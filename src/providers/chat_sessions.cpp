#include "providers/chat_sessions.hpp"

#include "core/kick_api.hpp"
#include "core/youtube_page.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <string_view>
#include <utility>

namespace goalbar {
namespace {

using namespace std::chrono_literals;

// How long to wait before looking again for a login, a live stream or a chat.
constexpr auto kIdleRetry = 30s;

// Every line of a Twitch WebSocket message ("\r\n" separated).
template<typename F> void for_each_line(std::string_view data, F &&f)
{
	while (!data.empty()) {
		const std::size_t end = data.find("\r\n");
		const std::string_view line = data.substr(0, end);
		if (!line.empty())
			f(line);
		if (end == std::string_view::npos)
			break;
		data.remove_prefix(end + 2);
	}
}

std::string http_error(const char *what, const HttpResult &result)
{
	return std::string(what) + (result.status ? " answered HTTP " + std::to_string(result.status)
						  : ": " + (result.error.empty() ? std::string("no response") : result.error));
}

unsigned guest_number()
{
	std::random_device random;
	return 10000 + random() % 90000;
}

// The consent cookies skip the EU cookie wall (same as the likes provider).
const HttpHeaders kYoutubeHeaders = {{"Cookie", "CONSENT=YES+1; SOCS=CAI"}, {"Accept-Language", "en-US,en;q=0.9"}};

} // namespace

ChatSession twitch_chat_session(ChatNet net, TwitchLoginLookup lookup)
{
	return [net = std::move(net), lookup = std::move(lookup)](ChatSessionHost &host) -> std::string {
		const std::optional<std::string> login = lookup(host.cancel());
		if (!login || login->empty()) {
			host.set_state(ProviderState::AuthRequired);
			host.wait(kIdleRetry);
			return {};
		}
		std::string error;
		const std::shared_ptr<ChatSocket> socket = net.connect(std::string(kTwitchChatUrl), {}, error);
		if (!socket)
			return "Twitch chat: " + error;
		host.attach(socket);
		for (const std::string &command : twitch_join_commands(*login, guest_number()))
			socket->send(command);

		std::string data;
		while (!host.stopping()) {
			if (!socket->receive(data))
				return host.stopping() ? std::string() : std::string("Twitch closed the chat connection");
			std::string failure;
			for_each_line(data, [&](std::string_view text) {
				const IrcLine line = parse_irc_line(text);
				if (line.command == "PING") {
					socket->send("PONG :" + (line.params.empty() ? std::string("tmi.twitch.tv")
										    : line.params.back()));
				} else if (line.command == "366" || line.command == "ROOMSTATE") {
					host.set_state(ProviderState::Live);
				} else if (line.command == "RECONNECT") {
					failure = "Twitch asked to reconnect";
				} else if (line.command == "NOTICE") {
					const auto id = line.tags.find("msg-id");
					if (id != line.tags.end() && id->second.rfind("msg_channel_suspended", 0) == 0)
						failure = "Twitch channel suspended or missing";
				} else if (const auto event = twitch_chat_event(line)) {
					host.add(*event);
				}
			});
			if (!failure.empty())
				return failure;
		}
		return {};
	};
}

ChatSession kick_chat_session(ChatNet net, std::string slug)
{
	return [net = std::move(net), slug = std::move(slug)](ChatSessionHost &host) -> std::string {
		const HttpHeaders json_headers = {{"Accept", "application/json"}};
		const HttpResult channel = net.get(kick_channel_url(slug), json_headers, host.cancel());
		if (host.stopping())
			return {};
		if (channel.status == 404)
			return kKickChannelNotFound;
		if (channel.status == 0)
			return http_error("Kick", channel);
		const std::optional<uint64_t> chatroom = kick_chatroom_id(channel.body);
		if (!chatroom)
			return channel.status >= 200 && channel.status < 300 ? std::string(kKickBlocked)
									      : http_error("Kick", channel);

		// Which Pusher app kick.com uses now; the last known one when this fails.
		const HttpResult realtime = net.post_json(std::string(kKickRealtimeUrl),
							   kick_realtime_request("goal-bar-" + std::to_string(guest_number())),
							   json_headers, host.cancel());
		const std::string url = kick_pusher_url(realtime.status >= 200 && realtime.status < 300 ? realtime.body
												       : std::string());
		if (host.stopping())
			return {};

		std::string error;
		const std::shared_ptr<ChatSocket> socket = net.connect(url, {{"Origin", "https://kick.com"}}, error);
		if (!socket)
			return "Kick chat: " + error;
		host.attach(socket);
		for (const std::string &subscribe : kick_subscribe_messages(*chatroom))
			socket->send(subscribe);

		std::string data;
		while (!host.stopping()) {
			if (!socket->receive(data))
				return host.stopping() ? std::string() : std::string("Kick closed the chat connection");
			const KickFrame frame = parse_kick_frame(data);
			switch (frame.kind) {
			case KickFrame::Kind::Ping:
				socket->send(std::string(kKickPong));
				break;
			case KickFrame::Kind::Subscribed:
				host.set_state(ProviderState::Live);
				break;
			case KickFrame::Kind::Error:
				return frame.message.empty() ? std::string("Kick chat error") : frame.message;
			case KickFrame::Kind::Chat:
			case KickFrame::Kind::Paid:
				host.add(frame.event);
				break;
			case KickFrame::Kind::Other:
				break;
			}
		}
		return {};
	};
}

ChatSession youtube_chat_session(ChatNet net, std::string channel_url)
{
	return [net = std::move(net), channel_url = std::move(channel_url)](ChatSessionHost &host) -> std::string {
		const HttpResult page = net.get(youtube_live_url(channel_url), kYoutubeHeaders, host.cancel());
		if (host.stopping())
			return {};
		if (page.status < 200 || page.status >= 300)
			return http_error("YouTube", page);
		const std::optional<std::string> video = parse_youtube_live_page(page.body).video_id;
		if (!video) {
			host.set_state(ProviderState::NotLive);
			host.wait(kIdleRetry);
			return {};
		}

		const HttpResult chat_page = net.get(youtube_chat_page_url(*video), kYoutubeHeaders, host.cancel());
		if (host.stopping())
			return {};
		if (chat_page.status < 200 || chat_page.status >= 300)
			return http_error("YouTube chat", chat_page);
		const YoutubeChatBootstrap boot = parse_youtube_chat_page(chat_page.body);
		if (boot.continuation.empty()) {
			// A live stream with its chat turned off: nothing to count.
			host.set_state(ProviderState::NotLive, "the live stream has no chat");
			host.wait(kIdleRetry);
			return {};
		}

		host.set_state(ProviderState::Live);
		const std::string poll_url = youtube_chat_poll_url(boot.api_key);
		std::string continuation = boot.continuation;
		bool first = true;
		while (!host.stopping() && !continuation.empty()) {
			const HttpResult answer = net.post_json(
				poll_url, youtube_chat_poll_body(boot.client_version, continuation), kYoutubeHeaders,
				host.cancel());
			if (host.stopping())
				return {};
			if (answer.status < 200 || answer.status >= 300)
				return http_error("YouTube chat", answer);
			const YoutubeChatBatch batch = parse_youtube_chat_batch(answer.body);
			if (!batch.valid)
				return {}; // the stream ended: look for the next one
			// The first batch is older chat history: it was written before the source started.
			if (!first) {
				for (const ChatEvent &event : batch.messages)
					host.add(event);
			}
			first = false;
			continuation = batch.continuation;
			if (host.wait(std::chrono::milliseconds(std::clamp(batch.timeout_ms, 1000, 6000))))
				return {};
		}
		return {};
	};
}

} // namespace goalbar

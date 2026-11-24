#include "obs/provider_factory.hpp"

#include "net/net.hpp"
#include "obs/twitch_services.hpp"
#include "providers/chat_count_provider.hpp"
#include "providers/chat_sessions.hpp"
#include "providers/kick_provider.hpp"
#include "providers/tiktok_provider.hpp"
#include "providers/twitch_provider.hpp"
#include "providers/youtube_public_provider.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <memory>
#include <optional>
#include <utility>

namespace goalbar::obs {
namespace {

// The consent cookies skip the EU cookie wall; English keeps the subscriber
// text in the format the parser reads.
const net::Headers kYoutubeHeaders = {{"Cookie", "CONSENT=YES+1; SOCS=CAI"},
				      {"Accept-Language", "en-US,en;q=0.9"}};

HttpResult fetch_youtube_page(const std::string &url, CancelToken &cancel)
{
	net::Response response = net::get(url, kYoutubeHeaders, 10000, &cancel);
	return HttpResult{response.status, std::move(response.body), std::move(response.error)};
}

// The default system user agent (desktop Chrome) is accepted by Kick's
// public endpoint (checked 2026-11-09), so only Accept is added.
const net::Headers kKickHeaders = {{"Accept", "application/json"}};

HttpResult fetch_kick_channel(const std::string &url, CancelToken &cancel)
{
	net::Response response = net::get(url, kKickHeaders, 10000, &cancel);
	return HttpResult{response.status, std::move(response.body), std::move(response.error)};
}

HttpResult fetch_tiktok_room(const std::string &url, CancelToken &cancel)
{
	net::Response response = net::get(url, tiktok_room_headers(), 10000, &cancel);
	return HttpResult{response.status, std::move(response.body), std::move(response.error)};
}

void log_provider(const std::string &message)
{
	obs_log(LOG_INFO, "%s", message.c_str());
}

HttpResult to_http_result(net::Response response)
{
	return HttpResult{response.status, std::move(response.body), std::move(response.error),
			  std::move(response.headers)};
}

class NetChatSocket final : public ChatSocket {
public:
	explicit NetChatSocket(std::shared_ptr<net::WebSocket> socket) : socket_(std::move(socket)) {}
	bool receive(std::string &message) override
	{
		return socket_->receive(message) == net::WebSocket::Result::Message;
	}
	bool send(const std::string &text) override { return socket_->send(text); }
	bool send_binary(const std::string &data) override { return socket_->send_binary(data); }
	void close() override { socket_->close(); }

private:
	std::shared_ptr<net::WebSocket> socket_;
};

ChatNet system_chat_net()
{
	ChatNet out;
	out.get = [](const std::string &url, const HttpHeaders &headers, CancelToken &cancel) {
		// 20 s: the TikTok sign server can take a while.
		return to_http_result(net::get(url, headers, 20000, &cancel));
	};
	out.post_json = [](const std::string &url, const std::string &json, const HttpHeaders &headers,
			   CancelToken &cancel) {
		return to_http_result(net::post(url, json, "application/json", headers, 10000, &cancel));
	};
	out.connect = [](const std::string &url, const HttpHeaders &headers,
			 std::string &error) -> std::shared_ptr<ChatSocket> {
		std::shared_ptr<net::WebSocket> socket = net::connect_websocket(url, headers, error);
		if (!socket)
			return nullptr;
		return std::make_shared<NetChatSocket>(std::move(socket));
	};
	return out;
}

// The connected account's login, whose chat a Twitch chat goal reads.
std::optional<std::string> twitch_login(CancelToken &cancel)
{
	const HelixUserResult result = twitch_account()->current_user(cancel);
	if (result.outcome != HelixOutcome::Ok || !result.user)
		return std::nullopt;
	return result.user->login;
}

} // namespace

std::unique_ptr<MetricProvider> make_youtube_provider(const std::string &channel_url, Metric metric)
{
	return std::make_unique<YoutubePublicProvider>(channel_url, metric, fetch_youtube_page, log_provider);
}

std::unique_ptr<MetricProvider> make_twitch_provider(Metric metric)
{
	return std::make_unique<TwitchProvider>(twitch_account(), metric, log_provider);
}

std::unique_ptr<MetricProvider> make_kick_provider(const std::string &slug, Metric metric)
{
	return std::make_unique<KickProvider>(fetch_kick_channel, slug, metric, log_provider);
}

std::unique_ptr<MetricProvider> make_tiktok_provider(const std::string &handle, Metric metric)
{
	return std::make_unique<TikTokProvider>(fetch_tiktok_room, handle, metric, log_provider);
}

std::unique_ptr<MetricProvider> make_chat_provider(Platform platform, Metric metric, int min_chars,
						   const std::string &channel)
{
	ChatSession session;
	switch (platform) {
	case Platform::Twitch:
		session = twitch_chat_session(system_chat_net(), twitch_login);
		break;
	case Platform::Kick:
		session = kick_chat_session(system_chat_net(), channel);
		break;
	case Platform::YouTube:
		session = youtube_chat_session(system_chat_net(), channel);
		break;
	case Platform::TikTok:
		session = tiktok_chat_session(system_chat_net(), channel, net::kUserAgent);
		break;
	default:
		return nullptr;
	}
	return std::make_unique<ChatCountProvider>(platform, metric, min_chars, std::move(session), log_provider);
}

} // namespace goalbar::obs

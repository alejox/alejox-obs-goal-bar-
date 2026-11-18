#include "core/chat_events.hpp"

#include <json.hpp>

#include <algorithm>
#include <utility>

namespace goalbar {
namespace {

using json = nlohmann::json;

bool is_space(char32_t c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || c == 0xA0 ||
	       c == 0x3000 || (c >= 0x2000 && c <= 0x200B);
}

// Decodes UTF-8 leniently: a malformed byte becomes one code point of its own.
std::vector<char32_t> code_points(std::string_view utf8)
{
	std::vector<char32_t> out;
	out.reserve(utf8.size());
	const std::size_t n = utf8.size();
	for (std::size_t i = 0; i < n;) {
		const auto lead = static_cast<unsigned char>(utf8[i]);
		std::size_t extra = 0;
		char32_t c = lead;
		if ((lead & 0xE0) == 0xC0) {
			extra = 1;
			c = lead & 0x1F;
		} else if ((lead & 0xF0) == 0xE0) {
			extra = 2;
			c = lead & 0x0F;
		} else if ((lead & 0xF8) == 0xF0) {
			extra = 3;
			c = lead & 0x07;
		}
		bool ok = lead < 0x80 || extra > 0;
		for (std::size_t k = 1; ok && k <= extra; ++k) {
			const auto next = i + k < n ? static_cast<unsigned char>(utf8[i + k]) : 0;
			ok = (next & 0xC0) == 0x80;
			c = (c << 6) | (next & 0x3F);
		}
		if (!ok) {
			out.push_back(lead);
			++i;
			continue;
		}
		out.push_back(c);
		i += extra + 1;
	}
	return out;
}

// Characters between the first and last non-space code point; positions in
// `emote` (one flag per code point) collapse each emote run to one character.
std::size_t visible_length(const std::vector<char32_t> &text, const std::vector<int> &emote)
{
	std::size_t first = 0;
	std::size_t last = text.size();
	while (first < last && emote[first] < 0 && is_space(text[first]))
		++first;
	while (last > first && emote[last - 1] < 0 && is_space(text[last - 1]))
		--last;
	std::size_t length = 0;
	for (std::size_t i = first; i < last; ++i) {
		if (emote[i] < 0 || i == first || emote[i - 1] != emote[i])
			++length;
	}
	return length;
}

std::string unescape_tag(std::string_view value)
{
	std::string out;
	out.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		const char c = value[i];
		if (c != '\\' || i + 1 >= value.size()) {
			if (c != '\\')
				out += c;
			continue;
		}
		const char n = value[++i];
		out += n == 's' ? ' ' : n == ':' ? ';' : n == 'r' ? '\r' : n == 'n' ? '\n' : n;
	}
	return out;
}

std::optional<uint64_t> to_uint(std::string_view text)
{
	if (text.empty() || text.size() > 19)
		return std::nullopt;
	uint64_t value = 0;
	for (char c : text) {
		if (c < '0' || c > '9')
			return std::nullopt;
		value = value * 10 + uint64_t(c - '0');
	}
	return value;
}

json parse_json(std::string_view text)
{
	return json::parse(text.begin(), text.end(), nullptr, false);
}

// A JSON value that may hold its object as a string (Pusher sends "data" both ways).
json unwrap(const json &value)
{
	if (value.is_string())
		return parse_json(value.get<std::string>());
	return value;
}

std::string capture_after(std::string_view text, std::string_view key)
{
	// "key":"value" — the value up to the next unescaped quote.
	const std::size_t at = text.find(key);
	if (at == std::string_view::npos)
		return {};
	std::size_t i = at + key.size();
	std::string out;
	for (; i < text.size() && text[i] != '"'; ++i) {
		if (text[i] == '\\' && i + 1 < text.size())
			++i;
		out += text[i];
	}
	return i < text.size() ? out : std::string();
}

} // namespace

std::size_t chat_text_length(std::string_view utf8)
{
	const std::vector<char32_t> text = code_points(utf8);
	return visible_length(text, std::vector<int>(text.size(), -1));
}

bool counts_as_comment(const ChatEvent &event, int min_chars)
{
	return event.is_message && event.length > 0 && event.length >= std::size_t(std::max(min_chars, 0));
}

void ChatTally::add(const ChatEvent &event)
{
	if (metric_ == Metric::Bits)
		total_ += event.bits;
	else if (metric_ == Metric::PaidSubs)
		total_ += event.paid_subs;
	else if (metric_ == Metric::Comments && counts_as_comment(event, min_chars_))
		++total_;
}

uint64_t CountCursor::take(uint64_t total)
{
	const uint64_t added = total >= applied_ ? total - applied_ : total;
	applied_ = total;
	return added;
}

// ---- Twitch -----------------------------------------------------------------

IrcLine parse_irc_line(std::string_view line)
{
	IrcLine out;
	while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
		line.remove_suffix(1);
	if (!line.empty() && line.front() == '@') {
		const std::size_t space = line.find(' ');
		std::string_view raw = line.substr(1, space == std::string_view::npos ? std::string_view::npos : space - 1);
		line = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
		while (!raw.empty()) {
			const std::size_t semi = raw.find(';');
			const std::string_view pair = raw.substr(0, semi);
			const std::size_t eq = pair.find('=');
			if (eq == std::string_view::npos)
				out.tags[std::string(pair)] = std::string();
			else
				out.tags[std::string(pair.substr(0, eq))] = unescape_tag(pair.substr(eq + 1));
			if (semi == std::string_view::npos)
				break;
			raw.remove_prefix(semi + 1);
		}
	}
	if (!line.empty() && line.front() == ':') {
		const std::size_t space = line.find(' ');
		out.prefix = std::string(line.substr(1, space == std::string_view::npos ? std::string_view::npos : space - 1));
		line = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
	}
	std::optional<std::string> trailing;
	const std::size_t colon = line.find(" :");
	if (colon != std::string_view::npos) {
		trailing = std::string(line.substr(colon + 2));
		line = line.substr(0, colon);
	} else if (!line.empty() && line.front() == ':') {
		trailing = std::string(line.substr(1));
		line = std::string_view();
	}
	while (!line.empty()) {
		const std::size_t space = line.find(' ');
		const std::string_view word = line.substr(0, space);
		if (!word.empty()) {
			if (out.command.empty())
				out.command = std::string(word);
			else
				out.params.emplace_back(word);
		}
		if (space == std::string_view::npos)
			break;
		line.remove_prefix(space + 1);
	}
	if (trailing)
		out.params.push_back(std::move(*trailing));
	return out;
}

std::vector<std::string> twitch_join_commands(std::string_view login, unsigned guest_number)
{
	return {"CAP REQ :twitch.tv/tags twitch.tv/commands", "PASS SCHMOOPIIE",
		"NICK justinfan" + std::to_string(guest_number), "JOIN #" + std::string(login)};
}

std::optional<ChatEvent> twitch_chat_event(const IrcLine &line)
{
	if (line.command != "PRIVMSG" || line.params.size() < 2)
		return std::nullopt;
	std::string body = line.params.back();
	const std::string action = "\x01" "ACTION ";
	if (body.rfind(action, 0) == 0 && body.size() > action.size() && body.back() == '\x01')
		body = body.substr(action.size(), body.size() - action.size() - 1);

	const std::vector<char32_t> text = code_points(body);
	std::vector<int> emote(text.size(), -1);
	const auto tag = line.tags.find("emotes");
	if (tag != line.tags.end()) {
		// "id:0-4,11-15/id2:6-9" with positions in code points, inclusive.
		int run = 0;
		std::string_view spec = tag->second;
		while (!spec.empty()) {
			const std::size_t slash = spec.find('/');
			std::string_view one = spec.substr(0, slash);
			const std::size_t colon = one.find(':');
			std::string_view ranges = colon == std::string_view::npos ? std::string_view() : one.substr(colon + 1);
			while (!ranges.empty()) {
				const std::size_t comma = ranges.find(',');
				const std::string_view range = ranges.substr(0, comma);
				const std::size_t dash = range.find('-');
				const auto from = to_uint(range.substr(0, dash));
				const auto to = dash == std::string_view::npos ? std::nullopt : to_uint(range.substr(dash + 1));
				if (from && to && *from <= *to && *to < text.size()) {
					for (uint64_t i = *from; i <= *to; ++i)
						emote[i] = run;
					++run;
				}
				if (comma == std::string_view::npos)
					break;
				ranges.remove_prefix(comma + 1);
			}
			if (slash == std::string_view::npos)
				break;
			spec.remove_prefix(slash + 1);
		}
	}

	ChatEvent event;
	event.is_message = true;
	event.length = visible_length(text, emote);
	const auto bits = line.tags.find("bits");
	if (bits != line.tags.end())
		event.bits = to_uint(bits->second).value_or(0);
	return event;
}

// ---- Kick -------------------------------------------------------------------

namespace {

// "[emote:37226:KEKW]" counts as one character.
std::size_t kick_content_length(std::string_view content)
{
	std::vector<char32_t> text;
	std::vector<int> emote;
	int run = 0;
	while (!content.empty()) {
		const std::size_t at = content.find("[emote:");
		const std::size_t end = at == std::string_view::npos ? std::string_view::npos : content.find(']', at);
		const std::string_view plain = content.substr(0, at == std::string_view::npos || end == std::string_view::npos
									 ? std::string_view::npos
									 : at);
		for (char32_t c : code_points(plain)) {
			text.push_back(c);
			emote.push_back(-1);
		}
		if (at == std::string_view::npos || end == std::string_view::npos)
			break;
		text.push_back(U'￼');
		emote.push_back(run++);
		content.remove_prefix(end + 1);
	}
	return visible_length(text, emote);
}

// Event names without Kick's old "App\\Events\\" prefix.
std::string_view kick_event_name(std::string_view event)
{
	constexpr std::string_view prefix = "App\\Events\\";
	if (event.substr(0, prefix.size()) == prefix)
		event.remove_prefix(prefix.size());
	return event;
}

// GiftedSubscriptionsEvent was seen live (2026-11-18); a plain subscription
// was not, so its known names are all accepted.
bool kick_gift_event(std::string_view event)
{
	return kick_event_name(event) == "GiftedSubscriptionsEvent";
}

bool kick_paid_event(std::string_view event)
{
	const std::string_view name = kick_event_name(event);
	return kick_gift_event(event) || name == "SubscriptionEvent" || name == "ChannelSubscriptionEvent";
}

constexpr std::string_view kKickFallbackKey = "34bf7a0ff419a2a775b9";
constexpr std::string_view kKickFallbackCluster = "us2";

std::string pusher_url(std::string_view key, std::string_view cluster)
{
	return "wss://ws-" + std::string(cluster) + ".pusher.com/app/" + std::string(key) +
	       "?protocol=7&client=js&version=8.4.0&flash=false";
}

} // namespace

KickFrame parse_kick_frame(std::string_view frame)
{
	KickFrame out;
	const json doc = parse_json(frame);
	if (!doc.is_object())
		return out;
	const std::string event = doc.value("event", std::string());
	if (event == "pusher:ping") {
		out.kind = KickFrame::Kind::Ping;
	} else if (event == "pusher_internal:subscription_succeeded") {
		out.kind = KickFrame::Kind::Subscribed;
	} else if (event == "pusher:error") {
		out.kind = KickFrame::Kind::Error;
		const json data = doc.contains("data") ? unwrap(doc["data"]) : json();
		if (data.is_object())
			out.message = "Pusher " + std::to_string(data.value("code", 0)) + ": " +
				      data.value("message", std::string());
	} else if (kick_paid_event(event) && doc.value("channel", std::string()).rfind("chatroom_", 0) == 0) {
		// Paid subscriptions, only from the chatroom_<id> channel (see kick_subscribe_messages).
		const json data = doc.contains("data") ? unwrap(doc["data"]) : json();
		if (data.is_object()) {
			out.kind = KickFrame::Kind::Paid;
			if (!kick_gift_event(event)) {
				out.event.paid_subs = 1;
			} else if (data.contains("gifted_usernames") && data["gifted_usernames"].is_array()) {
				// One per recipient: a big gift arrives in several chunks, each with its names.
				out.event.paid_subs = data["gifted_usernames"].size();
			}
		}
	} else if (event == "App\\Events\\ChatMessageEvent") {
		const json data = doc.contains("data") ? unwrap(doc["data"]) : json();
		if (data.is_object() && data.contains("content") && data["content"].is_string()) {
			out.kind = KickFrame::Kind::Chat;
			out.event.is_message = true;
			out.event.length = kick_content_length(data["content"].get<std::string>());
		}
	}
	return out;
}

std::string kick_subscribe_message(uint64_t chatroom_id)
{
	json message = {{"event", "pusher:subscribe"},
			{"data", {{"auth", ""}, {"channel", "chatrooms." + std::to_string(chatroom_id) + ".v2"}}}};
	return message.dump();
}

std::vector<std::string> kick_subscribe_messages(uint64_t chatroom_id)
{
	json events = {{"event", "pusher:subscribe"},
		       {"data", {{"auth", ""}, {"channel", "chatroom_" + std::to_string(chatroom_id)}}}};
	return {kick_subscribe_message(chatroom_id), events.dump()};
}

std::optional<uint64_t> kick_chatroom_id(std::string_view channel_json)
{
	const json doc = parse_json(channel_json);
	if (!doc.is_object() || !doc.contains("chatroom") || !doc["chatroom"].is_object())
		return std::nullopt;
	// Kick sends some numbers as strings of digits too ("id": 668 or "668").
	const json &id = doc["chatroom"]["id"];
	if (id.is_number_unsigned() || (id.is_number_integer() && id.get<int64_t>() >= 0))
		return id.get<uint64_t>();
	if (id.is_string())
		return to_uint(id.get<std::string>());
	return std::nullopt;
}

std::string kick_realtime_request(std::string_view client_id)
{
	json body = {{"client", {{"id", std::string(client_id)}, {"type", "web"}}},
		     {"capabilities", {{"accepted_providers", json::array({{{"provider", "pusher"}}})}}}};
	return body.dump();
}

std::string kick_pusher_url(std::string_view realtime_json)
{
	const json doc = parse_json(realtime_json);
	if (doc.is_object() && doc.contains("data") && doc["data"].is_object() && doc["data"].contains("connections") &&
	    doc["data"]["connections"].is_array()) {
		for (const json &connection : doc["data"]["connections"]) {
			if (!connection.is_object() || connection.value("provider", std::string()) != "pusher" ||
			    !connection.contains("credentials") || !connection["credentials"].is_object())
				continue;
			const json &credentials = connection["credentials"];
			const std::string key = credentials.value("app_key", std::string());
			const std::string cluster = credentials.value("cluster", std::string());
			if (!key.empty() && !cluster.empty())
				return pusher_url(key, cluster);
		}
	}
	return pusher_url(kKickFallbackKey, kKickFallbackCluster);
}

// ---- YouTube ----------------------------------------------------------------

std::string youtube_chat_page_url(std::string_view video_id)
{
	return "https://www.youtube.com/live_chat?is_popout=1&v=" + std::string(video_id);
}

YoutubeChatBootstrap parse_youtube_chat_page(std::string_view html)
{
	YoutubeChatBootstrap out;
	out.api_key = capture_after(html, "\"INNERTUBE_API_KEY\":\"");
	out.client_version = capture_after(html, "\"INNERTUBE_CONTEXT_CLIENT_VERSION\":\"");
	out.continuation = capture_after(html, "\"continuation\":\"");
	return out;
}

std::string youtube_chat_poll_url(std::string_view api_key)
{
	std::string url = "https://www.youtube.com/youtubei/v1/live_chat/get_live_chat";
	if (!api_key.empty())
		url += "?key=" + std::string(api_key);
	return url;
}

std::string youtube_chat_poll_body(std::string_view client_version, std::string_view continuation)
{
	const std::string version = client_version.empty() ? "2.20250101.00.00" : std::string(client_version);
	json body = {{"context", {{"client", {{"clientName", "WEB"}, {"clientVersion", version}}}}},
		     {"continuation", std::string(continuation)}};
	return body.dump();
}

namespace {

// Message runs: text counts per character, each emoji as one.
std::size_t youtube_runs_length(const json &message)
{
	std::vector<char32_t> text;
	std::vector<int> emote;
	int run = 0;
	if (!message.is_object() || !message.contains("runs") || !message["runs"].is_array())
		return 0;
	for (const json &part : message["runs"]) {
		if (!part.is_object())
			continue;
		if (part.contains("text") && part["text"].is_string()) {
			for (char32_t c : code_points(part["text"].get<std::string>())) {
				text.push_back(c);
				emote.push_back(-1);
			}
		} else if (part.contains("emoji")) {
			text.push_back(U'￼');
			emote.push_back(run++);
		}
	}
	return visible_length(text, emote);
}

} // namespace

YoutubeChatBatch parse_youtube_chat_batch(std::string_view text)
{
	YoutubeChatBatch out;
	const json doc = parse_json(text);
	if (!doc.is_object() || !doc.contains("continuationContents"))
		return out;
	const json &contents = doc["continuationContents"];
	if (!contents.is_object() || !contents.contains("liveChatContinuation") ||
	    !contents["liveChatContinuation"].is_object())
		return out;
	const json &chat = contents["liveChatContinuation"];
	out.valid = true;

	if (chat.contains("continuations") && chat["continuations"].is_array()) {
		for (const json &entry : chat["continuations"]) {
			if (!entry.is_object())
				continue;
			for (const char *kind :
			     {"invalidationContinuationData", "timedContinuationData", "reloadContinuationData"}) {
				if (!entry.contains(kind) || !entry[kind].is_object())
					continue;
				out.continuation = entry[kind].value("continuation", std::string());
				out.timeout_ms = entry[kind].value("timeoutMs", 3000);
				break;
			}
			if (!out.continuation.empty())
				break;
		}
	}

	if (chat.contains("actions") && chat["actions"].is_array()) {
		for (const json &action : chat["actions"]) {
			if (!action.is_object() || !action.contains("addChatItemAction"))
				continue;
			const json &add = action["addChatItemAction"];
			if (!add.is_object() || !add.contains("item") || !add["item"].is_object())
				continue;
			const json &item = add["item"];
			// Memberships: a new member or a renewal ("Member for 6 months"), and each
			// gift recipient. The gift purchase announcement is not counted: its
			// recipients each get their own redemption item.
			if (item.contains("liveChatMembershipItemRenderer") ||
			    item.contains("liveChatSponsorshipsGiftRedemptionAnnouncementRenderer")) {
				ChatEvent event;
				event.paid_subs = 1;
				out.messages.push_back(event);
				continue;
			}
			for (const char *renderer : {"liveChatTextMessageRenderer", "liveChatPaidMessageRenderer"}) {
				if (!item.contains(renderer) || !item[renderer].is_object())
					continue;
				ChatEvent event;
				event.is_message = true;
				const json &body = item[renderer];
				event.length = body.contains("message") ? youtube_runs_length(body["message"]) : 0;
				out.messages.push_back(event);
				break;
			}
		}
	}
	return out;
}

} // namespace goalbar

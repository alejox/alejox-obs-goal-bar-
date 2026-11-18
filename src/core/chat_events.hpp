// Pure core: no OBS, no Qt. What the live chats of Twitch, Kick and YouTube
// add to a counted goal (bits, comments): parsers for one chat frame or batch
// and the counting rules. The adapters do the network; these read the bytes.
//
// The formats are the ones multi-chat reads (src/chat/twitch.cpp, kick.cpp,
// youtube.cpp): Twitch IRC over WebSocket (anonymous), Kick's public Pusher
// channel and YouTube's live chat endpoint from its web player.
#pragma once

#include "core/metric.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace goalbar {

// One thing that happened in a chat.
struct ChatEvent {
	bool is_message = false; // a viewer wrote something (a comment)
	std::size_t length = 0;  // characters of the message; each emote counts as one
	uint64_t bits = 0;       // Twitch cheers
	uint64_t paid_subs = 0;  // paid subscriptions it announces (YouTube memberships, Kick subs)
};

// Characters (Unicode code points) of `utf8` without leading and trailing
// whitespace. Malformed bytes count one each.
std::size_t chat_text_length(std::string_view utf8);

// A message counts as a comment when it has at least `min_chars` characters
// (and never when it is empty).
bool counts_as_comment(const ChatEvent &event, int min_chars);

// Running total of a counted metric for one chat session.
class ChatTally {
public:
	ChatTally(Metric metric, int min_chars) : metric_(metric), min_chars_(min_chars) {}
	void add(const ChatEvent &event);
	uint64_t total() const { return total_; }

private:
	Metric metric_;
	int min_chars_;
	uint64_t total_ = 0;
};

// Hands out only what a provider's running total added since the last call.
// Reset it when the provider is replaced (its total starts again at zero).
class CountCursor {
public:
	uint64_t take(uint64_t total);
	void reset() { applied_ = 0; }

private:
	uint64_t applied_ = 0;
};

// ---- Twitch: IRC over WebSocket, anonymous and read-only -----------------

inline constexpr std::string_view kTwitchChatUrl = "wss://irc-ws.chat.twitch.tv:443";

struct IrcLine {
	std::map<std::string, std::string> tags; // unescaped values
	std::string prefix;
	std::string command;
	std::vector<std::string> params; // the trailing parameter last
};

IrcLine parse_irc_line(std::string_view line);

// The commands that log in anonymously and join `login`'s chat, in order.
std::vector<std::string> twitch_join_commands(std::string_view login, unsigned guest_number);

// A PRIVMSG as a chat event (with its bits); nullopt for anything else.
std::optional<ChatEvent> twitch_chat_event(const IrcLine &line);

// ---- Kick: Pusher WebSocket ------------------------------------------------

struct KickFrame {
	enum class Kind { Other, Ping, Subscribed, Error, Chat, Paid };
	Kind kind = Kind::Other;
	ChatEvent event;     // set for Chat and Paid
	std::string message; // set for Error
};

KickFrame parse_kick_frame(std::string_view frame);
std::string kick_subscribe_message(uint64_t chatroom_id);
// Both channels of a chatroom: chatrooms.<id>.v2 carries the chat messages,
// chatroom_<id> the paid subscriptions (checked 2026-11-18).
std::vector<std::string> kick_subscribe_messages(uint64_t chatroom_id);
inline constexpr std::string_view kKickPong = R"({"event":"pusher:pong","data":{}})";

// The chatroom id in a GET /api/v2/channels/<slug> answer.
std::optional<uint64_t> kick_chatroom_id(std::string_view channel_json);

// POST https://web.kick.com/api/v1/realtime/connection tells which Pusher app
// kick.com uses now (the key changes without notice).
inline constexpr std::string_view kKickRealtimeUrl = "https://web.kick.com/api/v1/realtime/connection";
std::string kick_realtime_request(std::string_view client_id);
// The Pusher URL from that answer, or the last known one when it has none.
std::string kick_pusher_url(std::string_view realtime_json);

// ---- YouTube: live chat from the web player --------------------------------

// GET https://www.youtube.com/live_chat?is_popout=1&v=<video>
std::string youtube_chat_page_url(std::string_view video_id);

struct YoutubeChatBootstrap {
	std::string api_key;
	std::string client_version;
	std::string continuation; // empty: the stream has no chat
};

YoutubeChatBootstrap parse_youtube_chat_page(std::string_view html);

// POST https://www.youtube.com/youtubei/v1/live_chat/get_live_chat[?key=…]
std::string youtube_chat_poll_url(std::string_view api_key);
std::string youtube_chat_poll_body(std::string_view client_version, std::string_view continuation);

struct YoutubeChatBatch {
	bool valid = false;          // the answer had a liveChatContinuation
	std::vector<ChatEvent> messages;
	std::string continuation;    // empty: the chat ended
	int timeout_ms = 3000;       // wait before the next poll
};

YoutubeChatBatch parse_youtube_chat_batch(std::string_view json);

} // namespace goalbar

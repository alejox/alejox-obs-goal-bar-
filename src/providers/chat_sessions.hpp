// Provider adapter: one chat connection per platform, for ChatCountProvider.
// The formats and rules live in core/chat_events.hpp; these do the talking.
#pragma once

#include "providers/chat_count_provider.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace goalbar {

// A silent WebSocket can be dead without the system noticing. After
// `ping_after` without any message the session pings the server, and after
// `give_up_after` it closes the connection, so the provider reconnects.
struct ChatKeepAlive {
	std::chrono::milliseconds ping_after;
	std::chrono::milliseconds give_up_after;
};

// Twitch pings every ~5 minutes on its own; Pusher asks the client to ping
// after its activity timeout (120 s) and to give up 30 s after an unanswered ping.
inline constexpr ChatKeepAlive kTwitchKeepAlive{std::chrono::minutes(4), std::chrono::minutes(5)};
inline constexpr ChatKeepAlive kKickKeepAlive{std::chrono::seconds(60), std::chrono::seconds(90)};

// The Twitch login whose chat to read, or nullopt when no account is
// connected (the session then waits as AuthRequired).
using TwitchLoginLookup = std::function<std::optional<std::string>(CancelToken &cancel)>;

// Twitch: anonymous, read-only IRC over WebSocket (no extra permission).
ChatSession twitch_chat_session(ChatNet net, TwitchLoginLookup login, ChatKeepAlive keep_alive = kTwitchKeepAlive);

// Kick: the channel's chatroom on Kick's public Pusher app. `slug` is normalized.
ChatSession kick_chat_session(ChatNet net, std::string slug, ChatKeepAlive keep_alive = kKickKeepAlive);

// YouTube: the live chat of the channel's current live stream; waits as
// NotLive while the channel is not live. `channel_url` is canonical (see
// normalize_youtube_channel).
ChatSession youtube_chat_session(ChatNet net, std::string channel_url);

} // namespace goalbar

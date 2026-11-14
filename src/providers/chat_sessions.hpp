// Provider adapter: one chat connection per platform, for ChatCountProvider.
// The formats and rules live in core/chat_events.hpp; these do the talking.
#pragma once

#include "providers/chat_count_provider.hpp"

#include <functional>
#include <optional>
#include <string>

namespace goalbar {

// The Twitch login whose chat to read, or nullopt when no account is
// connected (the session then waits as AuthRequired).
using TwitchLoginLookup = std::function<std::optional<std::string>(CancelToken &cancel)>;

// Twitch: anonymous, read-only IRC over WebSocket (no extra permission).
ChatSession twitch_chat_session(ChatNet net, TwitchLoginLookup login);

// Kick: the channel's chatroom on Kick's public Pusher app. `slug` is normalized.
ChatSession kick_chat_session(ChatNet net, std::string slug);

// YouTube: the live chat of the channel's current live stream; waits as
// NotLive while the channel is not live. `channel_url` is canonical (see
// normalize_youtube_channel).
ChatSession youtube_chat_session(ChatNet net, std::string channel_url);

} // namespace goalbar

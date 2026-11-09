// Pure core: no OBS, no Qt. Twitch Helix request URLs and response parsing.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace goalbar {

inline constexpr std::string_view kHelixEndpoint = "https://api.twitch.tv/helix";

// GET /users (the authenticated user).
std::string helix_users_url();
// GET /channels/followers?broadcaster_id=<id>&first=1
std::string helix_followers_url(std::string_view broadcaster_id);
// GET /subscriptions?broadcaster_id=<id>&first=1
std::string helix_subscriptions_url(std::string_view broadcaster_id);

struct TwitchUser {
	std::string id;
	std::string login;
	std::string display_name;
	std::string broadcaster_type; // "partner", "affiliate" or empty

	bool can_have_subscribers() const { return !broadcaster_type.empty(); }
};

// The first user of a GET /users answer; nullopt without an id.
std::optional<TwitchUser> parse_helix_user(std::string_view json);

// The `total` field of a followers or subscriptions answer.
std::optional<uint64_t> parse_helix_total(std::string_view json);

// Small JSON form for storing the user next to the account.
std::string encode_twitch_user(const TwitchUser &user);
std::optional<TwitchUser> decode_twitch_user(std::string_view text);

} // namespace goalbar

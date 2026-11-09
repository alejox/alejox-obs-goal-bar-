// Pure core: no OBS, no Qt. Kick public API (docs.kick.com): the app access
// token (OAuth client credentials) and GET /public/v1/channels. Builds the
// request URLs and bodies and parses the answers; the adapters do the HTTP.
#pragma once

#include "core/metric.hpp"
#include "core/metric_provider.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace goalbar {

inline constexpr std::string_view kKickTokenUrl = "https://id.kick.com/oauth/token";
inline constexpr std::string_view kKickApiEndpoint = "https://api.kick.com/public/v1";
inline constexpr std::string_view kKickDeveloperUrl = "https://kick.com/settings/developer";
inline constexpr std::size_t kKickSlugMaxLength = 25;

// Accepts "name", "@name", "kick.com/name" or a full channel URL and returns
// the lowercase channel slug; nullopt when it is empty, too long or has
// characters a slug cannot have.
std::optional<std::string> normalize_kick_slug(std::string_view input);

// GET /channels?slug=<slug>
std::string kick_channel_url(std::string_view slug);

// Form body of the client credentials grant. Never log it: it has the secret.
std::string kick_client_credentials_body(std::string_view client_id, std::string_view client_secret);

struct KickAppToken {
	std::string access_token;
	std::chrono::seconds expires_in{0}; // 0 when not sent
};

enum class KickTokenError {
	None,
	Network,       // no HTTP response
	InvalidClient, // the client ID or secret was rejected
	RateLimited,   // HTTP 429
	Other,
};

struct KickTokenResponse {
	std::optional<KickAppToken> token; // set only on success
	KickTokenError error = KickTokenError::None;
	std::string message; // Kick's error text, never a token or the secret
};

// `status` 0 means no response (KickTokenError::Network).
KickTokenResponse parse_kick_token_response(int status, std::string_view body);

// Re-acquire the app token this long before it expires.
inline constexpr std::chrono::seconds kKickTokenMargin{300};

// When a token obtained at `obtained` should be replaced: kKickTokenMargin
// before expiry, or at half its lifetime when that is shorter. Nullopt when
// the lifetime is unknown (rely on the 401).
std::optional<std::chrono::steady_clock::time_point>
kick_token_deadline(std::chrono::steady_clock::time_point obtained, std::chrono::seconds expires_in);

struct KickChannel {
	std::string slug;
	bool is_live = false;
	uint64_t viewer_count = 0;
	std::optional<uint64_t> active_subscribers; // nullopt when the answer lacks it
};

enum class KickChannelStatus { Found, NotFound, Malformed };

struct KickChannelLookup {
	KickChannelStatus status = KickChannelStatus::Malformed;
	std::optional<KickChannel> channel; // set when Found
};

// A 2xx answer of GET /channels?slug=. An empty `data` array means no channel
// has that slug.
KickChannelLookup parse_kick_channels(std::string_view json, std::string_view slug);

// MetricReading::message values the UI translates.
inline constexpr const char *kKickChannelNotFound = "Kick channel not found";
inline constexpr const char *kKickBadCredentials = "Kick app credentials were rejected";
inline constexpr const char *kKickNotSetUp = "Kick app not set up";

// Viewers follow the live status (NotLive while offline); active subscribers
// are readable at any time.
MetricReading kick_metric_reading(const KickChannel &channel, Metric metric);

} // namespace goalbar

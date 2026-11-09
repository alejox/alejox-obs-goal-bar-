// Pure core: no OBS, no Qt. Kick channel data from the public channel endpoint
// GET https://kick.com/api/v2/channels/<slug>, the one kick.com itself uses.
// It is undocumented, needs no credentials and sits behind Cloudflare, so it
// can change or be blocked at any time. Builds the URL and parses the answer;
// the adapters do the HTTP.
#pragma once

#include "core/metric.hpp"
#include "core/metric_provider.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace goalbar {

inline constexpr std::string_view kKickPublicEndpoint = "https://kick.com/api/v2";
inline constexpr std::size_t kKickSlugMaxLength = 25;

// Accepts "name", "@name", "kick.com/name" or a full channel URL and returns
// the lowercase channel slug; nullopt when it is empty, too long or has
// characters a slug cannot have.
std::optional<std::string> normalize_kick_slug(std::string_view input);

// GET /channels/<slug> on the public endpoint.
std::string kick_channel_url(std::string_view slug);

struct KickChannel {
	std::optional<uint64_t> followers;    // followers_count
	bool is_live = false;                 // livestream is an object with is_live true
	std::optional<uint64_t> viewer_count; // livestream.viewer_count while live
};

enum class KickChannelStatus {
	Found,
	NotFound,    // HTTP 404
	Blocked,     // HTTP 403, or a Cloudflare / HTML page instead of JSON
	RateLimited, // HTTP 429
	HttpError,   // any other non-2xx answer
	Malformed,   // 2xx JSON that is not a channel object
};

struct KickChannelLookup {
	KickChannelStatus status = KickChannelStatus::Malformed;
	int http_status = 0;
	std::optional<KickChannel> channel; // set when Found
};

// `status` is the HTTP status of an answer that arrived (not 0).
KickChannelLookup parse_kick_channel(int status, std::string_view body);

// MetricReading::message values the UI translates.
inline constexpr const char *kKickChannelNotFound = "Kick channel not found";
inline constexpr const char *kKickBlocked = "Kick blocked the request";

// Followers are readable at any time; viewers follow the live status
// (NotLive while offline). A failed lookup becomes its error state, never a
// made-up 0. Other metrics are an error: Kick does not publish them.
MetricReading kick_metric_reading(const KickChannelLookup &lookup, Metric metric);

} // namespace goalbar

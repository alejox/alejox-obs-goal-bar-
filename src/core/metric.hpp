// Pure core: no OBS, no Qt.
#pragma once

#include <optional>
#include <string_view>

namespace goalbar {

// Bits, Comments, PaidSubs and Diamonds are counted from the live chat while
// the source runs; the others are read from the platform as a total. PaidSubs
// are YouTube memberships and Kick subscriptions (new, renewed and gifted):
// those platforms publish no total of paid subscribers. Twitch Subscribers are
// the paid total from its API. Diamonds are what TikTok gifts are worth.
// TikTok Likes are the live room's total, which only its chat tells.
enum class Metric { Likes, Subscribers, Followers, Viewers, Bits, Comments, PaidSubs, Diamonds };

enum class Platform { Manual, YouTube, Twitch, Kick, TikTok };

// Stable ids used for settings persistence. Never change an existing id.
std::string_view metric_id(Metric metric);
std::optional<Metric> metric_from_id(std::string_view id);

std::string_view platform_id(Platform platform);
std::optional<Platform> platform_from_id(std::string_view id);

// Whether the metric is counted from chat events (bits, comments) rather than
// read as a total: the goal then grows by what the chat adds.
bool metric_is_counted(Metric metric);

// Whether `platform` reads `metric` from its live chat: the counted metrics,
// and TikTok Likes (a total, but only the chat tells it).
bool metric_from_chat(Platform platform, Metric metric);

// Which metrics each platform can read. Manual takes any value but viewers and
// the counted metrics.
bool platform_reads(Platform platform, Metric metric);

// `metric` when `platform` reads it, otherwise the platform's first metric
// (how settings saved with a metric the platform no longer reads migrate).
Metric readable_metric(Platform platform, Metric metric);

} // namespace goalbar

// Pure core: no OBS, no Qt.
#pragma once

#include <optional>
#include <string_view>

namespace goalbar {

// Bits, Comments and PaidSubs are counted from the live chat while the
// source runs; the others are read from the platform as a total. PaidSubs are
// YouTube memberships and Kick subscriptions (new, renewed and gifted): those
// platforms publish no total of paid subscribers. Twitch Subscribers are the
// paid total from its API.
enum class Metric { Likes, Subscribers, Followers, Viewers, Bits, Comments, PaidSubs };

enum class Platform { Manual, YouTube, Twitch, Kick };

// Stable ids used for settings persistence. Never change an existing id.
std::string_view metric_id(Metric metric);
std::optional<Metric> metric_from_id(std::string_view id);

std::string_view platform_id(Platform platform);
std::optional<Platform> platform_from_id(std::string_view id);

// Whether the metric is counted from chat events (bits, comments) rather than
// read as a total: the goal then grows by what the chat adds.
bool metric_is_counted(Metric metric);

// Which metrics each platform can read. Manual takes any value but viewers and
// the counted metrics.
bool platform_reads(Platform platform, Metric metric);

// `metric` when `platform` reads it, otherwise the platform's first metric
// (how settings saved with a metric the platform no longer reads migrate).
Metric readable_metric(Platform platform, Metric metric);

} // namespace goalbar

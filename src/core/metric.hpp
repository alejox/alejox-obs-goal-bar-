// Pure core: no OBS, no Qt.
#pragma once

#include <optional>
#include <string_view>

namespace goalbar {

enum class Metric { Likes, Subscribers, Followers, Viewers };

enum class Platform { Manual, YouTube, Twitch, Kick };

// Stable ids used for settings persistence. Never change an existing id.
std::string_view metric_id(Metric metric);
std::optional<Metric> metric_from_id(std::string_view id);

std::string_view platform_id(Platform platform);
std::optional<Platform> platform_from_id(std::string_view id);

// Which metrics each platform can read. Manual takes any value but viewers.
bool platform_reads(Platform platform, Metric metric);

// `metric` when `platform` reads it, otherwise the platform's first metric
// (how settings saved with a metric the platform no longer reads migrate).
Metric readable_metric(Platform platform, Metric metric);

} // namespace goalbar

// Pure core: no OBS, no Qt.
#pragma once

#include <optional>
#include <string_view>

namespace goalbar {

enum class Metric { Likes, Subscribers, Followers };

enum class Platform { Manual, YouTube, Twitch };

// Stable ids used for settings persistence. Never change an existing id.
std::string_view metric_id(Metric metric);
std::optional<Metric> metric_from_id(std::string_view id);

std::string_view platform_id(Platform platform);
std::optional<Platform> platform_from_id(std::string_view id);

} // namespace goalbar

// Pure core: no OBS, no Qt.
#pragma once

#include "core/goal.hpp"
#include "core/metric.hpp"

#include <cstdint>
#include <map>
#include <string>

namespace goalbar {

using SettingsMap = std::map<std::string, std::string>;

inline constexpr uint64_t kDefaultGoalTarget = 100;
inline constexpr uint64_t kDefaultGoalStep = 100;

// Everything persisted for one Goal Bar source.
struct GoalSettings {
	Goal goal{0, kDefaultGoalTarget, 0, false};
	Metric metric = Metric::Likes;
	Platform platform = Platform::Manual;
	uint64_t step = kDefaultGoalStep;
};

// Flat string map, so adapters can map it onto obs_data without core knowing OBS.
SettingsMap encode_goal_settings(const GoalSettings &settings);

// Missing or malformed values fall back to the GoalSettings defaults. Never throws.
GoalSettings decode_goal_settings(const SettingsMap &values);

} // namespace goalbar

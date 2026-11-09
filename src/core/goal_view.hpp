// Pure core: no OBS, no Qt.
#pragma once

#include "core/goal_settings.hpp"

#include <string>

namespace goalbar {

// Everything a renderer needs to draw one Goal Bar frame.
struct GoalView {
	std::string title;
	std::string value_label; // "{current} / {target}", compact numbers
	double progress = 0.0;   // [0, 1]
	bool reached = false;    // current value is at or above the target
};

GoalView build_goal_view(const GoalSettings &settings, const std::string &title);

} // namespace goalbar

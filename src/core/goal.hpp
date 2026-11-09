// Pure core: no OBS, no Qt.
#pragma once

#include <cstdint>
#include <optional>

namespace goalbar {

enum class GoalEvent { None, GoalReached };

// One goal: progress is measured from `start` (the metric value when the goal
// was set) to `target`. The reached event fires at most once per goal.
struct Goal {
	uint64_t start = 0;
	uint64_t target = 1;
	uint64_t current = 0;
	bool reached_fired = false;

	// Fraction of the way from start to target, clamped to [0, 1].
	double progress() const;

	// Records a new metric value. Returns GoalReached the first time the value
	// reaches the target; later drops below the target never re-arm the goal.
	GoalEvent apply(uint64_t new_value);

	// The next goal: starts at this target and moves up by `step` until the
	// target is above the current value. Returns nullopt when step is 0.
	std::optional<Goal> advance(uint64_t step) const;
};

} // namespace goalbar

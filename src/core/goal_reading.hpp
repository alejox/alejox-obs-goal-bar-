// Pure core: no OBS, no Qt.
#pragma once

#include "core/goal.hpp"

#include <cstdint>

namespace goalbar {

// Baseline: the first value a provider delivers after it is (re)created.
// Update: every later value.
enum class ReadingKind { Baseline, Update };

struct ReadingOutcome {
	GoalEvent event = GoalEvent::None;
	bool changed = false; // the goal differs from before and should be persisted
};

// Lands one provider value on `goal`. A baseline never celebrates: a value
// already at or above the target marks the goal reached silently, and with
// `auto_next_step` > 0 the goal advances silently until its target is above
// the value. Updates go through Goal::apply (fire once).
ReadingOutcome apply_metric_value(Goal &goal, uint64_t value, ReadingKind kind, uint64_t auto_next_step);

} // namespace goalbar

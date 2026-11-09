#include "core/goal.hpp"

#include <limits>

namespace goalbar {

double Goal::progress() const
{
	if (target <= start)
		return current >= target ? 1.0 : 0.0;
	if (current <= start)
		return 0.0;
	if (current >= target)
		return 1.0;
	return static_cast<double>(current - start) / static_cast<double>(target - start);
}

GoalEvent Goal::apply(uint64_t new_value)
{
	current = new_value;
	if (reached_fired || current < target)
		return GoalEvent::None;
	reached_fired = true;
	return GoalEvent::GoalReached;
}

void Goal::set_target(uint64_t new_target)
{
	target = new_target;
	if (current < target)
		reached_fired = false;
}

void Goal::reset()
{
	start = current;
	reached_fired = false;
}

std::optional<Goal> Goal::advance(uint64_t step) const
{
	if (step == 0)
		return std::nullopt;

	constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
	// Smallest number of steps that puts the new target above the current value.
	const uint64_t steps = current >= target ? (current - target) / step + 1 : 1;
	const bool overflows = steps > (max_value - target) / step;
	const uint64_t next_target = overflows ? max_value : target + steps * step;

	return Goal{target, next_target, current, false};
}

} // namespace goalbar

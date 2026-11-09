#include "core/goal_reading.hpp"

namespace goalbar {
namespace {

bool same_goal(const Goal &a, const Goal &b)
{
	return a.start == b.start && a.target == b.target && a.current == b.current &&
	       a.reached_fired == b.reached_fired;
}

} // namespace

ReadingOutcome apply_metric_value(Goal &goal, uint64_t value, ReadingKind kind, uint64_t auto_next_step)
{
	const Goal before = goal;
	ReadingOutcome outcome;
	if (kind == ReadingKind::Update) {
		outcome.event = goal.apply(value);
	} else {
		goal.current = value;
		if (value >= goal.target) {
			goal.reached_fired = true;
			if (auto_next_step > 0)
				goal = goal.advance(auto_next_step).value_or(goal);
		}
	}
	outcome.changed = !same_goal(before, goal);
	return outcome;
}


void reset_goal(Goal &goal, uint64_t auto_next_step)
{
	goal.reset();
	if (goal.current < goal.target)
		return;
	goal.reached_fired = true;
	if (auto_next_step == 0)
		return;
	goal = goal.advance(auto_next_step).value_or(goal);
	goal.start = goal.current;
}

} // namespace goalbar

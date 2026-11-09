#include "core/goal_reading.hpp"

#include "test_harness.hpp"

using namespace goalbar;

TEST_CASE(baseline_below_target_only_records_the_value)
{
	Goal goal{0, 100, 0, false};
	const ReadingOutcome outcome = apply_metric_value(goal, 40, ReadingKind::Baseline, 100);
	CHECK_EQ(outcome.event, GoalEvent::None);
	CHECK(outcome.changed);
	CHECK_EQ(goal.current, uint64_t{40});
	CHECK_EQ(goal.target, uint64_t{100});
	CHECK(!goal.reached_fired);
}

TEST_CASE(baseline_above_target_marks_reached_silently)
{
	Goal goal{0, 100, 0, false};
	const ReadingOutcome outcome = apply_metric_value(goal, 150, ReadingKind::Baseline, 0);
	CHECK_EQ(outcome.event, GoalEvent::None);
	CHECK(outcome.changed);
	CHECK(goal.reached_fired);
	CHECK_EQ(goal.target, uint64_t{100});

	// Later updates keep the fire-once rule: no celebration for this goal.
	CHECK_EQ(apply_metric_value(goal, 160, ReadingKind::Update, 0).event, GoalEvent::None);
}

TEST_CASE(baseline_above_target_with_auto_next_advances_silently)
{
	Goal goal{0, 100, 0, false};
	const ReadingOutcome outcome = apply_metric_value(goal, 350, ReadingKind::Baseline, 100);
	CHECK_EQ(outcome.event, GoalEvent::None);
	CHECK(outcome.changed);
	CHECK_EQ(goal.current, uint64_t{350});
	CHECK_EQ(goal.target, uint64_t{400});
	CHECK(!goal.reached_fired);

	// The advanced goal is armed again.
	CHECK_EQ(apply_metric_value(goal, 400, ReadingKind::Update, 100).event, GoalEvent::GoalReached);
}

TEST_CASE(baseline_advances_a_goal_that_already_fired)
{
	// Celebrated, then OBS quit before the auto-next advance ran.
	Goal goal{0, 100, 120, true};
	apply_metric_value(goal, 130, ReadingKind::Baseline, 50);
	CHECK_EQ(goal.target, uint64_t{150});
	CHECK(!goal.reached_fired);
}

TEST_CASE(baseline_with_unchanged_value_reports_no_change)
{
	Goal goal{0, 100, 40, false};
	CHECK(!apply_metric_value(goal, 40, ReadingKind::Baseline, 100).changed);
}

TEST_CASE(update_crossing_the_target_fires_once)
{
	Goal goal{0, 100, 90, false};
	const ReadingOutcome first = apply_metric_value(goal, 100, ReadingKind::Update, 100);
	CHECK_EQ(first.event, GoalEvent::GoalReached);
	CHECK(first.changed);
	// Updates never advance by themselves: the action sequence does it later.
	CHECK_EQ(goal.target, uint64_t{100});
	CHECK_EQ(apply_metric_value(goal, 110, ReadingKind::Update, 100).event, GoalEvent::None);
}

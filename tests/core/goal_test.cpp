#include "core/goal.hpp"

#include "test_harness.hpp"

using namespace goalbar;

// The bar matches the "current / target" label, whatever the goal started at.
TEST_CASE(progress_is_current_over_target)
{
	CHECK_NEAR((Goal{0, 200, 50, false}.progress()), 0.25, 1e-9);
	CHECK_NEAR((Goal{500, 1000, 514, false}.progress()), 0.514, 1e-9);
	CHECK_NEAR((Goal{100, 200, 200, false}.progress()), 1.0, 1e-9);
}

TEST_CASE(progress_is_clamped)
{
	CHECK_NEAR((Goal{0, 200, 0, false}.progress()), 0.0, 1e-9);
	CHECK_NEAR((Goal{100, 200, 500, false}.progress()), 1.0, 1e-9);
}

TEST_CASE(apply_fires_reached_exactly_once)
{
	Goal goal{0, 10, 0, false};
	CHECK(goal.apply(5) == GoalEvent::None);
	CHECK_EQ(goal.current, uint64_t{5});
	CHECK(goal.apply(10) == GoalEvent::GoalReached);
	CHECK(goal.reached_fired);
	CHECK(goal.apply(11) == GoalEvent::None);
	CHECK(goal.apply(10) == GoalEvent::None);
}

TEST_CASE(dropping_below_target_does_not_rearm)
{
	Goal goal{0, 10, 0, false};
	CHECK(goal.apply(12) == GoalEvent::GoalReached);
	CHECK(goal.apply(8) == GoalEvent::None);
	CHECK(goal.reached_fired);
	CHECK(goal.apply(10) == GoalEvent::None);
	CHECK_EQ(goal.current, uint64_t{10});
}

TEST_CASE(persisted_fired_flag_suppresses_event)
{
	Goal goal{0, 10, 10, true};
	CHECK(goal.apply(15) == GoalEvent::None);
}

TEST_CASE(advance_moves_to_next_step)
{
	const Goal goal{0, 100, 105, true};
	const auto next = goal.advance(50);
	REQUIRE(next.has_value());
	CHECK_EQ(next->start, uint64_t{100});
	CHECK_EQ(next->target, uint64_t{150});
	CHECK_EQ(next->current, uint64_t{105});
	CHECK(!next->reached_fired);
}

TEST_CASE(advance_skips_targets_already_passed)
{
	const Goal goal{0, 100, 260, true};
	const auto next = goal.advance(50);
	REQUIRE(next.has_value());
	CHECK_EQ(next->start, uint64_t{100});
	CHECK_EQ(next->target, uint64_t{300});
	CHECK(!next->reached_fired);

	const auto exact = Goal{0, 100, 250, true}.advance(50);
	REQUIRE(exact.has_value());
	CHECK_EQ(exact->target, uint64_t{300});
}

TEST_CASE(advance_rejects_zero_step)
{
	CHECK(!(Goal{0, 100, 100, true}.advance(0).has_value()));
}

TEST_CASE(set_target_above_current_rearms_the_goal)
{
	Goal goal{0, 10, 12, true};
	goal.set_target(20);
	CHECK_EQ(goal.target, uint64_t{20});
	CHECK(!goal.reached_fired);
	CHECK(goal.apply(20) == GoalEvent::GoalReached);
}

TEST_CASE(set_target_at_or_below_current_keeps_the_fired_flag)
{
	Goal goal{0, 10, 12, true};
	goal.set_target(12);
	CHECK(goal.reached_fired);
	CHECK(goal.apply(12) == GoalEvent::None);
}

TEST_CASE(reset_restarts_progress_from_the_current_value)
{
	Goal goal{0, 10, 12, true};
	goal.reset();
	CHECK_EQ(goal.start, uint64_t{12});
	CHECK_EQ(goal.current, uint64_t{12});
	CHECK(!goal.reached_fired);
}

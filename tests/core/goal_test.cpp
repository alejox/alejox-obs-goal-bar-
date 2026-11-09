#include "core/goal.hpp"

#include "test_harness.hpp"

using namespace goalbar;

TEST_CASE(progress_is_measured_from_start)
{
	CHECK_NEAR((Goal{100, 200, 100, false}.progress()), 0.0, 1e-9);
	CHECK_NEAR((Goal{100, 200, 150, false}.progress()), 0.5, 1e-9);
	CHECK_NEAR((Goal{100, 200, 200, false}.progress()), 1.0, 1e-9);
}

TEST_CASE(progress_is_clamped)
{
	CHECK_NEAR((Goal{100, 200, 50, false}.progress()), 0.0, 1e-9);
	CHECK_NEAR((Goal{100, 200, 500, false}.progress()), 1.0, 1e-9);
}

TEST_CASE(progress_with_target_not_above_start_is_all_or_nothing)
{
	CHECK_NEAR((Goal{200, 200, 199, false}.progress()), 0.0, 1e-9);
	CHECK_NEAR((Goal{200, 200, 200, false}.progress()), 1.0, 1e-9);
	CHECK_NEAR((Goal{300, 200, 250, false}.progress()), 1.0, 1e-9);
	CHECK_NEAR((Goal{300, 200, 150, false}.progress()), 0.0, 1e-9);
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

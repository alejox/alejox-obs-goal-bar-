#include "core/progress_tween.hpp"

#include "test_harness.hpp"

using namespace goalbar;

TEST_CASE(ease_out_starts_fast_and_ends_at_one)
{
	CHECK_NEAR(ease_out_cubic(0.0), 0.0, 1e-9);
	CHECK_NEAR(ease_out_cubic(1.0), 1.0, 1e-9);
	CHECK_NEAR(ease_out_cubic(0.5), 0.875, 1e-9);
	// Clamped outside [0, 1].
	CHECK_NEAR(ease_out_cubic(-1.0), 0.0, 1e-9);
	CHECK_NEAR(ease_out_cubic(2.0), 1.0, 1e-9);
}

TEST_CASE(jump_sets_the_value_without_animating)
{
	ProgressTween tween;
	tween.jump(0.4);
	CHECK_NEAR(tween.value(), 0.4, 1e-9);
	CHECK(!tween.animating());
	CHECK(!tween.tick(0.016));
}

TEST_CASE(retarget_eases_out_over_the_duration)
{
	ProgressTween tween(0.6);
	tween.jump(0.0);
	tween.retarget(1.0);
	CHECK(tween.animating());
	CHECK_NEAR(tween.value(), 0.0, 1e-9);

	CHECK(tween.tick(0.3));
	CHECK_NEAR(tween.value(), 0.875, 1e-9); // ease-out: past the midpoint at half time
	CHECK(tween.tick(0.3));
	CHECK_NEAR(tween.value(), 1.0, 1e-9);
	// The last tick lands exactly and reports the change; then it stops.
	CHECK(!tween.animating());
	CHECK(!tween.tick(0.016));
}

TEST_CASE(retarget_mid_flight_starts_from_the_shown_value)
{
	ProgressTween tween(0.6);
	tween.jump(0.0);
	tween.retarget(1.0);
	tween.tick(0.3); // shown 0.875
	tween.retarget(0.5);
	CHECK_NEAR(tween.value(), 0.875, 1e-9);
	tween.tick(0.6);
	CHECK_NEAR(tween.value(), 0.5, 1e-9);
}

TEST_CASE(retarget_to_the_same_value_is_a_no_op)
{
	ProgressTween tween;
	tween.jump(0.25);
	tween.retarget(0.25);
	CHECK(!tween.animating());
}

TEST_CASE(values_are_clamped_to_the_unit_range)
{
	ProgressTween tween;
	tween.jump(-3.0);
	CHECK_NEAR(tween.value(), 0.0, 1e-9);
	tween.retarget(7.0);
	tween.tick(10.0);
	CHECK_NEAR(tween.value(), 1.0, 1e-9);
}

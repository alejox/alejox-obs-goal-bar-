#include "core/subscriber_rounding.hpp"

#include "test_harness.hpp"

using namespace goalbar;

TEST_CASE(display_step_follows_three_significant_figures)
{
	CHECK_EQ(subscriber_display_step(0), uint64_t{1});
	CHECK_EQ(subscriber_display_step(999), uint64_t{1});
	CHECK_EQ(subscriber_display_step(1000), uint64_t{10});
	CHECK_EQ(subscriber_display_step(1234), uint64_t{10});
	CHECK_EQ(subscriber_display_step(9999), uint64_t{10});
	CHECK_EQ(subscriber_display_step(12345), uint64_t{100});
	CHECK_EQ(subscriber_display_step(123456), uint64_t{1000});
	CHECK_EQ(subscriber_display_step(1234567), uint64_t{10000});
}

TEST_CASE(target_is_rounded_up_to_display_step)
{
	CHECK_EQ(align_target_to_step(995, 990), uint64_t{995});
	CHECK_EQ(align_target_to_step(1235, 1234), uint64_t{1240});
	CHECK_EQ(align_target_to_step(1240, 1234), uint64_t{1240});
	CHECK_EQ(align_target_to_step(12301, 12345), uint64_t{12400});
	CHECK_EQ(align_target_to_step(150001, 123456), uint64_t{151000});
	// The API rounds at the magnitude of the value it reports when the goal is hit,
	// so a target past a digit boundary aligns to the coarser step.
	CHECK_EQ(align_target_to_step(10050, 9990), uint64_t{10100});
	CHECK_EQ(align_target_to_step(1005, 999), uint64_t{1010});
}

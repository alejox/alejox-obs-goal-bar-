#include "core/poll_schedule.hpp"

#include "test_harness.hpp"

using namespace goalbar;
using std::chrono::seconds;

TEST_CASE(no_failures_polls_at_the_interval)
{
	CHECK_EQ(next_poll_delay(seconds{30}, 0).count(), 30);
	CHECK_EQ(next_poll_delay(seconds{60}, 0).count(), 60);
}

TEST_CASE(interval_is_raised_to_the_minimum)
{
	CHECK_EQ(next_poll_delay(seconds{5}, 0).count(), kMinPollInterval.count());
	CHECK_EQ(next_poll_delay(seconds{0}, 0).count(), kMinPollInterval.count());
}

TEST_CASE(failures_back_off_exponentially_up_to_the_cap)
{
	CHECK_EQ(next_poll_delay(seconds{30}, 1).count(), 60);
	CHECK_EQ(next_poll_delay(seconds{30}, 2).count(), 120);
	CHECK_EQ(next_poll_delay(seconds{30}, 3).count(), 240);
	CHECK_EQ(next_poll_delay(seconds{30}, 4).count(), 300);
	CHECK_EQ(next_poll_delay(seconds{30}, 1000).count(), 300);
}

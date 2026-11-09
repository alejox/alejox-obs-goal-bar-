#include "core/manual_provider.hpp"

#include "test_harness.hpp"

#include <limits>

using namespace goalbar;

TEST_CASE(manual_provider_reports_live_value)
{
	ManualProvider provider(Metric::Subscribers, 42);
	CHECK(provider.platform() == Platform::Manual);
	CHECK(provider.metric() == Metric::Subscribers);
	const MetricReading reading = provider.latest();
	CHECK(reading.state == ProviderState::Live);
	CHECK(reading.value == uint64_t{42});
}

TEST_CASE(manual_provider_set_and_increment)
{
	ManualProvider provider;
	provider.set_value(10);
	CHECK(provider.latest().value == uint64_t{10});
	provider.increment(5);
	CHECK(provider.latest().value == uint64_t{15});
	provider.increment(-3);
	CHECK(provider.latest().value == uint64_t{12});
}

TEST_CASE(manual_provider_increment_saturates)
{
	ManualProvider provider(Metric::Likes, 3);
	provider.increment(-10);
	CHECK(provider.latest().value == uint64_t{0});
	provider.increment(std::numeric_limits<int64_t>::min());
	CHECK(provider.latest().value == uint64_t{0});

	provider.set_value(std::numeric_limits<uint64_t>::max() - 1);
	provider.increment(5);
	CHECK(provider.latest().value == std::numeric_limits<uint64_t>::max());
}

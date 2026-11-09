#include "core/manual_provider.hpp"

#include <limits>

namespace goalbar {

ManualProvider::ManualProvider(Metric metric, uint64_t value) : metric_(metric), value_(value) {}

Platform ManualProvider::platform() const
{
	return Platform::Manual;
}

Metric ManualProvider::metric() const
{
	return metric_;
}

MetricReading ManualProvider::latest() const
{
	return {value_, ProviderState::Live, {}};
}

void ManualProvider::set_value(uint64_t value)
{
	value_ = value;
}

void ManualProvider::increment(int64_t delta)
{
	if (delta >= 0) {
		const uint64_t amount = static_cast<uint64_t>(delta);
		const uint64_t headroom = std::numeric_limits<uint64_t>::max() - value_;
		value_ = amount > headroom ? std::numeric_limits<uint64_t>::max() : value_ + amount;
	} else {
		// Negate in unsigned arithmetic so INT64_MIN does not overflow.
		const uint64_t amount = 0 - static_cast<uint64_t>(delta);
		value_ = amount > value_ ? 0 : value_ - amount;
	}
}

} // namespace goalbar

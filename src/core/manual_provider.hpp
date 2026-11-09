// Pure core: no OBS, no Qt.
#pragma once

#include "core/metric_provider.hpp"

namespace goalbar {

// A value typed or nudged by the user. Always Live.
class ManualProvider final : public MetricProvider {
public:
	explicit ManualProvider(Metric metric = Metric::Likes, uint64_t value = 0);

	Platform platform() const override;
	Metric metric() const override;
	MetricReading latest() const override;

	void set_value(uint64_t value);
	// Adds `delta` to the value, saturating at 0 and at the uint64 maximum.
	void increment(int64_t delta);

private:
	Metric metric_;
	uint64_t value_;
};

} // namespace goalbar

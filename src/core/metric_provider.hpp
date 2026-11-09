// Pure core: no OBS, no Qt.
#pragma once

#include "core/metric.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace goalbar {

enum class ProviderState { Idle, Connecting, Live, NotLive, AuthRequired, RateLimited, Error };

// The latest known value plus the provider state, for display in the UI.
struct MetricReading {
	std::optional<uint64_t> value;
	ProviderState state = ProviderState::Idle;
	std::string message;
};

// Port implemented by metric adapters (manual, YouTube, Twitch). `latest()`
// must never block: adapters poll in the background and cache the reading.
class MetricProvider {
public:
	virtual ~MetricProvider() = default;

	virtual Platform platform() const = 0;
	virtual Metric metric() const = 0;
	virtual MetricReading latest() const = 0;
};

} // namespace goalbar

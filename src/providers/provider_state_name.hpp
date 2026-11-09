// Log names of provider states, shared by the provider adapters.
#pragma once

#include "core/metric_provider.hpp"

namespace goalbar {

inline const char *provider_state_name(ProviderState state)
{
	switch (state) {
	case ProviderState::Idle:
		return "idle";
	case ProviderState::Connecting:
		return "connecting";
	case ProviderState::Live:
		return "live";
	case ProviderState::NotLive:
		return "not live";
	case ProviderState::AuthRequired:
		return "auth required";
	case ProviderState::RateLimited:
		return "rate limited";
	case ProviderState::Error:
		return "error";
	}
	return "unknown";
}

} // namespace goalbar

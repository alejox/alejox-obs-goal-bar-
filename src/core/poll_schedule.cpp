#include "core/poll_schedule.hpp"

#include <algorithm>

namespace goalbar {

std::chrono::seconds next_poll_delay(std::chrono::seconds interval, unsigned consecutive_failures)
{
	std::chrono::seconds delay = std::max(interval, kMinPollInterval);
	for (unsigned i = 0; i < consecutive_failures && delay < kMaxBackoff; ++i)
		delay *= 2;
	return std::min(delay, std::max(kMaxBackoff, std::max(interval, kMinPollInterval)));
}

} // namespace goalbar

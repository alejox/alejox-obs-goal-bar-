#include "core/subscriber_rounding.hpp"

#include <algorithm>
#include <limits>

namespace goalbar {

uint64_t subscriber_display_step(uint64_t count)
{
	// Each digit past the third significant figure multiplies the step by 10.
	uint64_t step = 1;
	for (uint64_t remaining = count; remaining >= 1000; remaining /= 10)
		step *= 10;
	return step;
}

uint64_t align_target_to_step(uint64_t target, uint64_t count)
{
	// The goal is judged against the value reported once the target is reached,
	// so use the coarser step of the two magnitudes.
	const uint64_t step = subscriber_display_step(std::max(target, count));
	const uint64_t remainder = target % step;
	if (remainder == 0)
		return target;
	const uint64_t padding = step - remainder;
	if (target > std::numeric_limits<uint64_t>::max() - padding)
		return target - remainder;
	return target + padding;
}

} // namespace goalbar

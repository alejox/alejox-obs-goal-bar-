// Pure core: no OBS, no Qt.
#pragma once

#include <cstdint>

namespace goalbar {

// YouTube Data API rounds subscriberCount down to 3 significant figures above
// 1000, so a subscriber count only moves in steps of this size:
// < 1000 -> 1, 1234 -> 10, 12345 -> 100, 123456 -> 1000.
uint64_t subscriber_display_step(uint64_t count);

// Rounds `target` up to a multiple of the display step for `count`, so the
// goal is reachable by a value YouTube can actually report.
uint64_t align_target_to_step(uint64_t target, uint64_t count);

} // namespace goalbar

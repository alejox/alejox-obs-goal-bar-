// Pure core: no OBS, no Qt.
#pragma once

#include <cstdint>
#include <string>

namespace goalbar {

// Short display form of a count: plain below 1000, otherwise up to 3
// significant figures with a K/M/B/T suffix and trailing zeros removed.
// Truncates rather than rounds, so a count never looks larger than it is:
// 999 -> "999", 1234 -> "1.23K", 12345 -> "12.3K", 1000000 -> "1M".
std::string format_compact(uint64_t value);

} // namespace goalbar

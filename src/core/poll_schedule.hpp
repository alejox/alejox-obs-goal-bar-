// Pure core: no OBS, no Qt.
#pragma once

#include <chrono>

namespace goalbar {

inline constexpr std::chrono::seconds kDefaultPollInterval{30};
inline constexpr std::chrono::seconds kMinPollInterval{20};
inline constexpr std::chrono::seconds kMaxBackoff{300};

// Delay before the next poll. With no failures it is `interval` (raised to the
// minimum); each consecutive failure doubles it, capped at kMaxBackoff.
std::chrono::seconds next_poll_delay(std::chrono::seconds interval, unsigned consecutive_failures);

} // namespace goalbar

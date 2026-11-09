// Twitch application of obs-goal-bar. It is registered as a public client, so
// the id is not a secret and there is no client secret: login uses the Device
// Code Grant Flow (the only flow Twitch allows for public clients).
#pragma once

namespace goalbar {

inline constexpr const char *kTwitchClientId = "p8sei2nbhepih66gtscohm8qpaicfw";

} // namespace goalbar

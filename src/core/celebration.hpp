// Pure core: no OBS, no Qt.
#pragma once

#include <cstdint>
#include <vector>

namespace goalbar {

inline constexpr double kCelebrationSeconds = 4.0;
inline constexpr int kConfettiColors = 5;
inline constexpr int kDefaultConfettiCount = 48;

// One confetti piece, in coordinates relative to the source area.
struct ConfettiParticle {
	double x = 0.0;     // fraction of the width, roughly [0, 1]
	double y = 0.0;     // fraction of the height, roughly [-0.1, 1.1]
	double size = 0.0;  // fraction of the height
	double angle = 0.0; // radians
	double alpha = 0.0; // [0, 1]
	int color = 0;      // palette slot in [0, kConfettiColors)
};

// What the renderer draws on top of the bar at one moment of a celebration.
struct CelebrationFrame {
	bool active = false;
	double glow = 0.0; // [0, 1], pulse strength around the bar
	std::vector<ConfettiParticle> confetti;
};

// Deterministic: the same seed and time always give the same frame. Inactive
// (no glow, no confetti) outside [0, kCelebrationSeconds).
CelebrationFrame celebration_frame(uint32_t seed, double elapsed_seconds, int particle_count = kDefaultConfettiCount);

} // namespace goalbar

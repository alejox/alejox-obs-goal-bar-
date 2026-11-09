#include "core/celebration.hpp"

#include <algorithm>
#include <cmath>

namespace goalbar {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFadeIn = 0.15;  // seconds
constexpr double kFadeOut = 0.8;  // seconds
constexpr double kPulseHz = 1.5;  // glow pulses per second
constexpr double kFallSpan = 1.3; // confetti wraps over [-0.15, 1.15] of the height

// splitmix64: a tiny, well-mixed, platform-independent generator.
class Random {
public:
	explicit Random(uint64_t seed) : state_(seed) {}

	// Uniform in [0, 1).
	double next()
	{
		state_ += 0x9E3779B97F4A7C15ULL;
		uint64_t z = state_;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		z ^= z >> 31;
		return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
	}

	double between(double low, double high) { return low + (high - low) * next(); }

private:
	uint64_t state_;
};

// Fades in quickly and out over the last kFadeOut seconds.
double envelope(double t)
{
	const double in = std::min(1.0, t / kFadeIn);
	const double out = std::clamp((kCelebrationSeconds - t) / kFadeOut, 0.0, 1.0);
	return in * out;
}

ConfettiParticle particle_at(uint32_t seed, int index, double t, double alpha)
{
	Random random((static_cast<uint64_t>(seed) << 32) ^ static_cast<uint64_t>(index) * 0x2545F4914F6CDD1DULL);
	const double x0 = random.next();
	const double phase = random.next();      // where in the fall it starts
	const double speed = random.between(0.6, 1.2); // heights per second
	const double sway = random.between(0.4, 1.0);  // sways per second
	const double spin = random.between(-6.0, 6.0); // radians per second

	ConfettiParticle particle;
	particle.x = x0 + 0.02 * std::sin(2.0 * kPi * (sway * t + phase));
	particle.y = std::fmod(phase * kFallSpan + speed * t, kFallSpan) - 0.15;
	particle.size = random.between(0.06, 0.12);
	particle.angle = random.between(0.0, 2.0 * kPi) + spin * t;
	particle.alpha = alpha;
	particle.color = static_cast<int>(random.next() * kConfettiColors) % kConfettiColors;
	return particle;
}

} // namespace

CelebrationFrame celebration_frame(uint32_t seed, double elapsed_seconds, int particle_count)
{
	CelebrationFrame frame;
	if (elapsed_seconds < 0.0 || elapsed_seconds >= kCelebrationSeconds)
		return frame;

	const double alpha = envelope(elapsed_seconds);
	frame.active = true;
	frame.glow = alpha * (0.5 + 0.5 * std::cos(2.0 * kPi * kPulseHz * elapsed_seconds));
	frame.confetti.reserve(static_cast<size_t>(std::max(0, particle_count)));
	for (int i = 0; i < particle_count; ++i)
		frame.confetti.push_back(particle_at(seed, i, elapsed_seconds, alpha));
	return frame;
}

} // namespace goalbar

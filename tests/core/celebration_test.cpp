#include "core/celebration.hpp"

#include "test_harness.hpp"

using namespace goalbar;

namespace {

bool same_frame(const CelebrationFrame &a, const CelebrationFrame &b)
{
	if (a.active != b.active || a.glow != b.glow || a.confetti.size() != b.confetti.size())
		return false;
	for (size_t i = 0; i < a.confetti.size(); ++i) {
		const ConfettiParticle &p = a.confetti[i];
		const ConfettiParticle &q = b.confetti[i];
		if (p.x != q.x || p.y != q.y || p.size != q.size || p.angle != q.angle || p.alpha != q.alpha ||
		    p.color != q.color)
			return false;
	}
	return true;
}

} // namespace

TEST_CASE(frames_are_deterministic)
{
	CHECK(same_frame(celebration_frame(7, 1.25), celebration_frame(7, 1.25)));
	CHECK(!same_frame(celebration_frame(7, 1.25), celebration_frame(8, 1.25)));
	CHECK(!same_frame(celebration_frame(7, 1.25), celebration_frame(7, 1.30)));
}

TEST_CASE(frame_is_inactive_outside_the_celebration)
{
	CHECK(!celebration_frame(1, -0.1).active);
	CHECK(!celebration_frame(1, kCelebrationSeconds).active);
	CHECK(celebration_frame(1, kCelebrationSeconds).confetti.empty());
	CHECK_NEAR(celebration_frame(1, kCelebrationSeconds + 2).glow, 0.0, 1e-12);
}

TEST_CASE(frame_has_the_requested_particles_in_bounds)
{
	for (const double t : {0.0, 0.5, 1.7, 2.9, 3.95}) {
		const CelebrationFrame frame = celebration_frame(42, t, 30);
		REQUIRE(frame.active);
		REQUIRE(frame.confetti.size() == 30);
		CHECK(frame.glow >= 0.0 && frame.glow <= 1.0);
		for (const ConfettiParticle &p : frame.confetti) {
			CHECK(p.x >= -0.1 && p.x <= 1.1);
			CHECK(p.y >= -0.15 && p.y <= 1.15);
			CHECK(p.size > 0.0 && p.size <= 0.2);
			CHECK(p.alpha >= 0.0 && p.alpha <= 1.0);
			CHECK(p.color >= 0 && p.color < kConfettiColors);
		}
	}
}

TEST_CASE(frame_fades_out_at_the_end)
{
	const CelebrationFrame middle = celebration_frame(3, 2.0);
	const CelebrationFrame end = celebration_frame(3, kCelebrationSeconds - 0.02);
	REQUIRE(!middle.confetti.empty() && !end.confetti.empty());
	CHECK(middle.confetti.front().alpha > 0.9);
	CHECK(end.confetti.front().alpha < 0.05);
	CHECK(end.glow < 0.05);
}

TEST_CASE(confetti_moves_over_time)
{
	const CelebrationFrame early = celebration_frame(5, 0.5);
	const CelebrationFrame later = celebration_frame(5, 0.6);
	REQUIRE(early.confetti.size() == later.confetti.size());
	CHECK(early.confetti.front().y != later.confetti.front().y);
}

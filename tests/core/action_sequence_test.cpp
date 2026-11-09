#include "core/action_sequence.hpp"

#include "core/celebration.hpp"
#include "test_harness.hpp"

#include <vector>

using namespace goalbar;

namespace {

using Commands = std::vector<ActionCommand>;

ActionPlan celebration_only()
{
	return ActionPlan{true, false, false, 8.0, true};
}

ActionPlan timed_source(double seconds)
{
	return ActionPlan{true, true, false, seconds, true};
}

ActionPlan media_source()
{
	return ActionPlan{true, true, true, 8.0, true};
}

} // namespace

TEST_CASE(celebration_then_advance_then_idle)
{
	ActionSequence sequence;
	CHECK(sequence.start(celebration_only(), false).empty());
	CHECK_EQ(sequence.phase(), ActionPhase::Celebrating);
	CHECK(sequence.celebrating());

	CHECK(sequence.tick(kCelebrationSeconds - 0.1).empty());
	CHECK(sequence.celebrating());

	CHECK(sequence.tick(0.2) == Commands{ActionCommand::Advance});
	CHECK_EQ(sequence.phase(), ActionPhase::Advancing);
	CHECK(!sequence.celebrating());

	sequence.advance_done();
	CHECK_EQ(sequence.phase(), ActionPhase::Idle);
	CHECK(sequence.tick(1.0).empty());
}

TEST_CASE(test_run_never_advances)
{
	ActionSequence sequence;
	sequence.start(celebration_only(), true);
	CHECK(sequence.tick(kCelebrationSeconds + 1.0).empty());
	CHECK_EQ(sequence.phase(), ActionPhase::Idle);
}

TEST_CASE(no_auto_next_ends_idle)
{
	ActionPlan plan = celebration_only();
	plan.auto_next = false;
	ActionSequence sequence;
	sequence.start(plan, false);
	CHECK(sequence.tick(kCelebrationSeconds + 1.0).empty());
	CHECK_EQ(sequence.phase(), ActionPhase::Idle);
}

TEST_CASE(timed_source_longer_than_celebration_delays_the_advance)
{
	ActionSequence sequence;
	CHECK(sequence.start(timed_source(8.0), false) == Commands{ActionCommand::ShowSource});
	CHECK(sequence.tick(kCelebrationSeconds + 0.1).empty());
	CHECK(!sequence.celebrating());
	CHECK_EQ(sequence.phase(), ActionPhase::Celebrating);
	CHECK(sequence.tick(4.0) == (Commands{ActionCommand::HideSource, ActionCommand::Advance}));
}

TEST_CASE(timed_source_shorter_than_celebration_hides_first)
{
	ActionSequence sequence;
	sequence.start(timed_source(1.0), false);
	CHECK(sequence.tick(1.0) == Commands{ActionCommand::HideSource});
	CHECK(sequence.celebrating());
	CHECK(sequence.tick(kCelebrationSeconds) == Commands{ActionCommand::Advance});
}

TEST_CASE(media_source_hides_when_it_ends)
{
	ActionSequence sequence;
	CHECK(sequence.start(media_source(), false) == Commands{ActionCommand::ShowSource});
	CHECK(sequence.tick(20.0).empty());
	CHECK(sequence.media_ended() == (Commands{ActionCommand::HideSource, ActionCommand::Advance}));
	// A second end signal changes nothing.
	CHECK(sequence.media_ended().empty());
}

TEST_CASE(media_source_is_capped)
{
	ActionSequence sequence;
	sequence.start(media_source(), false);
	CHECK(sequence.tick(kMaxMediaSeconds) == (Commands{ActionCommand::HideSource, ActionCommand::Advance}));
}

TEST_CASE(media_end_while_idle_is_ignored)
{
	ActionSequence sequence;
	CHECK(sequence.media_ended().empty());
}

TEST_CASE(nothing_to_show_advances_at_once)
{
	ActionPlan plan = celebration_only();
	plan.celebrate = false;
	ActionSequence sequence;
	CHECK(sequence.start(plan, false) == Commands{ActionCommand::Advance});
	CHECK_EQ(sequence.phase(), ActionPhase::Advancing);
}

TEST_CASE(test_during_a_real_celebration_keeps_the_advance)
{
	ActionSequence sequence;
	sequence.start(celebration_only(), false);
	sequence.tick(1.0);
	sequence.start(celebration_only(), true);
	CHECK_NEAR(sequence.elapsed(), 0.0, 1e-9);
	CHECK(sequence.tick(kCelebrationSeconds) == Commands{ActionCommand::Advance});
}

TEST_CASE(restart_without_source_hides_the_shown_one)
{
	ActionSequence sequence;
	sequence.start(timed_source(8.0), true);
	CHECK(sequence.start(celebration_only(), true) == Commands{ActionCommand::HideSource});
}

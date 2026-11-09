#include "core/action_sequence.hpp"

#include "core/celebration.hpp"
#include "test_harness.hpp"

#include <vector>

using namespace goalbar;

namespace {

using Commands = std::vector<ActionCommand>;

ActionPlan celebration_only()
{
	return ActionPlan{true, false, false, 8.0, true, false};
}

ActionPlan timed_source(double seconds)
{
	return ActionPlan{true, true, false, seconds, true, false};
}

ActionPlan media_source()
{
	return ActionPlan{true, true, true, 8.0, true, false};
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

TEST_CASE(looping_media_hides_after_show_seconds)
{
	ActionPlan plan = media_source();
	plan.source_loops = true;
	plan.show_seconds = 12.0;
	ActionSequence sequence;
	sequence.start(plan, false);
	CHECK(sequence.tick(11.9).empty());
	CHECK(sequence.tick(0.1) == (Commands{ActionCommand::HideSource, ActionCommand::Advance}));
}

TEST_CASE(looping_media_still_hides_early_when_it_ends)
{
	ActionPlan plan = media_source();
	plan.source_loops = true;
	plan.show_seconds = 60.0;
	ActionSequence sequence;
	sequence.start(plan, false);
	sequence.tick(kCelebrationSeconds);
	CHECK(sequence.media_ended() == (Commands{ActionCommand::HideSource, ActionCommand::Advance}));
}

// --- v2 actions: scene switch, replay clip, screenshot -------------------------

namespace {

ActionPlan nothing_but(void (*set)(ActionPlan &))
{
	ActionPlan plan = celebration_only();
	plan.auto_next = false;
	set(plan);
	return plan;
}

} // namespace

TEST_CASE(switch_scene_runs_at_start_and_stays_without_return)
{
	ActionSequence sequence;
	const ActionPlan plan = nothing_but([](ActionPlan &p) { p.switch_scene = true; });
	CHECK(sequence.start(plan, false) == Commands{ActionCommand::SwitchScene});
	CHECK(sequence.tick(700.0).empty());
	CHECK(!sequence.has_pending());
}

TEST_CASE(return_scene_fires_after_return_seconds_even_after_the_advance)
{
	ActionPlan plan = timed_source(1.0);
	plan.switch_scene = true;
	plan.return_seconds = 10.0;
	ActionSequence sequence;
	CHECK(sequence.start(plan, false) == (Commands{ActionCommand::ShowSource, ActionCommand::SwitchScene}));
	CHECK(sequence.tick(1.0) == Commands{ActionCommand::HideSource});
	CHECK(sequence.tick(kCelebrationSeconds) == Commands{ActionCommand::Advance});
	sequence.advance_done();
	CHECK_EQ(sequence.phase(), ActionPhase::Idle);
	CHECK(sequence.has_pending());
	CHECK(sequence.tick(10.0 - 1.0 - kCelebrationSeconds - 0.1).empty());
	CHECK(sequence.tick(0.2) == Commands{ActionCommand::ReturnScene});
	CHECK(!sequence.has_pending());
}

TEST_CASE(replay_is_saved_after_its_delay)
{
	ActionSequence sequence;
	ActionPlan plan = nothing_but([](ActionPlan &p) {
		p.save_replay = true;
		p.replay_delay = 5.0;
	});
	CHECK(sequence.start(plan, false).empty());
	CHECK(sequence.tick(4.9).empty());
	CHECK(sequence.tick(0.2) == Commands{ActionCommand::SaveReplay});
	CHECK(sequence.tick(10.0).empty());
}

TEST_CASE(replay_with_no_delay_is_saved_at_start)
{
	ActionSequence sequence;
	ActionPlan plan = nothing_but([](ActionPlan &p) {
		p.save_replay = true;
		p.replay_delay = 0.0;
	});
	CHECK(sequence.start(plan, false) == Commands{ActionCommand::SaveReplay});
}

TEST_CASE(test_run_never_saves_a_replay)
{
	ActionSequence sequence;
	ActionPlan plan = nothing_but([](ActionPlan &p) {
		p.save_replay = true;
		p.replay_delay = 0.0;
	});
	CHECK(sequence.start(plan, true).empty());
	CHECK(sequence.tick(60.0).empty());
}

TEST_CASE(screenshot_after_its_delay_also_in_tests)
{
	ActionSequence sequence;
	ActionPlan plan = nothing_but([](ActionPlan &p) { p.take_screenshot = true; });
	CHECK(sequence.start(plan, true).empty());
	CHECK(sequence.tick(kScreenshotDelaySeconds - 0.1).empty());
	CHECK(sequence.tick(0.2) == Commands{ActionCommand::TakeScreenshot});
}

TEST_CASE(commands_due_on_the_same_tick_keep_their_order)
{
	ActionSequence sequence;
	ActionPlan plan = nothing_but([](ActionPlan &p) {
		p.take_screenshot = true;
		p.save_replay = true;
		p.replay_delay = 2.0;
	});
	sequence.start(plan, false);
	CHECK(sequence.tick(3.0) == (Commands{ActionCommand::TakeScreenshot, ActionCommand::SaveReplay}));
}

TEST_CASE(restart_keeps_a_pending_return_and_real_replay)
{
	ActionPlan real = nothing_but([](ActionPlan &p) {
		p.switch_scene = true;
		p.return_seconds = 10.0;
		p.save_replay = true;
		p.replay_delay = 5.0;
	});
	ActionSequence sequence;
	sequence.start(real, false);
	sequence.tick(1.0);
	// A test run without v2 actions must not drop the real run's pending work.
	CHECK(sequence.start(celebration_only(), true).empty());
	CHECK(sequence.tick(4.0) == Commands{ActionCommand::SaveReplay});
	CHECK(sequence.tick(5.0) == Commands{ActionCommand::ReturnScene});
}

TEST_CASE(restart_with_switch_reschedules_the_return)
{
	ActionPlan plan = nothing_but([](ActionPlan &p) {
		p.switch_scene = true;
		p.return_seconds = 10.0;
	});
	ActionSequence sequence;
	sequence.start(plan, false);
	sequence.tick(8.0);
	CHECK(sequence.start(plan, true) == Commands{ActionCommand::SwitchScene});
	CHECK(sequence.tick(9.0).empty());
	CHECK(sequence.tick(1.0) == Commands{ActionCommand::ReturnScene});
}

TEST_CASE(restart_with_switch_and_stay_drops_the_return)
{
	ActionPlan plan = nothing_but([](ActionPlan &p) {
		p.switch_scene = true;
		p.return_seconds = 10.0;
	});
	ActionSequence sequence;
	sequence.start(plan, false);
	plan.return_seconds = 0.0;
	sequence.start(plan, false);
	CHECK(sequence.tick(20.0).empty());
}

#include "core/action_sequence.hpp"

#include "core/celebration.hpp"

#include <algorithm>
#include <utility>

namespace goalbar {

std::vector<ActionCommand> ActionSequence::start(const ActionPlan &plan, bool test)
{
	std::vector<ActionCommand> commands;
	if (plan.show_source)
		commands.push_back(ActionCommand::ShowSource); // also restarts a shown one
	else if (source_shown_)
		commands.push_back(ActionCommand::HideSource);

	plan_ = plan;
	phase_ = ActionPhase::Celebrating;
	elapsed_ = 0.0;
	source_shown_ = plan.show_source;
	advance_pending_ = advance_pending_ || (!test && plan.auto_next);

	if (plan.switch_scene) {
		drop(ActionCommand::ReturnScene);
		commands.push_back(ActionCommand::SwitchScene);
		if (plan.return_seconds > 0.0)
			schedule(ActionCommand::ReturnScene, plan.return_seconds, commands);
	}
	if (plan.save_replay && !test)
		schedule(ActionCommand::SaveReplay, plan.replay_delay, commands);
	if (plan.take_screenshot)
		schedule(ActionCommand::TakeScreenshot, kScreenshotDelaySeconds, commands);

	for (const ActionCommand command : finish_if_done())
		commands.push_back(command);
	return commands;
}

std::vector<ActionCommand> ActionSequence::tick(double seconds)
{
	std::vector<ActionCommand> commands = run_due_timers(seconds);
	if (phase_ != ActionPhase::Celebrating)
		return commands;
	elapsed_ += seconds;
	if (source_shown_ && source_time_over()) {
		source_shown_ = false;
		commands.push_back(ActionCommand::HideSource);
	}
	for (const ActionCommand command : finish_if_done())
		commands.push_back(command);
	return commands;
}

std::vector<ActionCommand> ActionSequence::media_ended()
{
	if (phase_ != ActionPhase::Celebrating || !source_shown_ || !plan_.source_is_media)
		return {};
	source_shown_ = false;
	std::vector<ActionCommand> commands{ActionCommand::HideSource};
	for (const ActionCommand command : finish_if_done())
		commands.push_back(command);
	return commands;
}

void ActionSequence::advance_done()
{
	if (phase_ == ActionPhase::Advancing)
		phase_ = ActionPhase::Idle;
}

bool ActionSequence::celebrating() const
{
	return phase_ == ActionPhase::Celebrating && plan_.celebrate && elapsed_ < kCelebrationSeconds;
}

// Ends the Celebrating phase once the animation and the shown source are both over.
std::vector<ActionCommand> ActionSequence::finish_if_done()
{
	if (phase_ != ActionPhase::Celebrating || celebrating() || source_shown_)
		return {};
	if (!advance_pending_) {
		phase_ = ActionPhase::Idle;
		return {};
	}
	advance_pending_ = false;
	phase_ = ActionPhase::Advancing;
	return {ActionCommand::Advance};
}

bool ActionSequence::source_time_over() const
{
	const bool until_media_end = plan_.source_is_media && !plan_.source_loops;
	return elapsed_ >= (until_media_end ? kMaxMediaSeconds : plan_.show_seconds);
}

void ActionSequence::drop(ActionCommand command)
{
	timed_.erase(std::remove_if(timed_.begin(), timed_.end(),
				    [command](const Timed &t) { return t.command == command; }),
		     timed_.end());
}

// Replaces a pending command of the same kind; a zero delay runs it now.
void ActionSequence::schedule(ActionCommand command, double delay, std::vector<ActionCommand> &now)
{
	drop(command);
	if (delay <= 0.0)
		now.push_back(command);
	else
		timed_.push_back(Timed{command, delay});
}

// Due commands run in the order they fell due.
std::vector<ActionCommand> ActionSequence::run_due_timers(double seconds)
{
	if (timed_.empty())
		return {};
	for (Timed &t : timed_)
		t.remaining -= seconds;
	std::vector<Timed> due;
	std::vector<Timed> waiting;
	for (const Timed &t : timed_)
		(t.remaining > 0.0 ? waiting : due).push_back(t);
	timed_ = std::move(waiting);
	std::stable_sort(due.begin(), due.end(),
			 [](const Timed &a, const Timed &b) { return a.remaining < b.remaining; });
	std::vector<ActionCommand> commands;
	for (const Timed &t : due)
		commands.push_back(t.command);
	return commands;
}

} // namespace goalbar

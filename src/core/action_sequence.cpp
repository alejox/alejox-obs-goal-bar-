#include "core/action_sequence.hpp"

#include "core/celebration.hpp"

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

	for (const ActionCommand command : finish_if_done())
		commands.push_back(command);
	return commands;
}

std::vector<ActionCommand> ActionSequence::tick(double seconds)
{
	if (phase_ != ActionPhase::Celebrating)
		return {};
	elapsed_ += seconds;
	std::vector<ActionCommand> commands;
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
	return elapsed_ >= (plan_.source_is_media ? kMaxMediaSeconds : plan_.show_seconds);
}

} // namespace goalbar

#include "core/goal_view.hpp"

#include "core/compact_number.hpp"

namespace goalbar {

GoalView build_goal_view(const GoalSettings &settings, const std::string &title, const std::string &status)
{
	const Goal &goal = settings.goal;
	GoalView view;
	view.title = title;
	view.value_label = format_compact(goal.current) + " / " + format_compact(goal.target);
	view.progress = goal.progress();
	view.reached = goal.current >= goal.target;
	if (!status.empty())
		view.value_label += " \u00b7 " + status;
	return view;
}

} // namespace goalbar

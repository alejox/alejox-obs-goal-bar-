#include "core/goal_view.hpp"

#include "test_harness.hpp"

#include <string>

using namespace goalbar;

namespace {

GoalSettings settings_with(Goal goal)
{
	GoalSettings settings;
	settings.goal = goal;
	return settings;
}

} // namespace

TEST_CASE(view_carries_title_and_compact_value_label)
{
	const GoalView view = build_goal_view(settings_with(Goal{0, 2000, 1234, false}), "Likes goal");
	CHECK_EQ(view.title, std::string("Likes goal"));
	CHECK_EQ(view.value_label, std::string("1.23K / 2K"));
}

TEST_CASE(view_progress_follows_goal)
{
	const GoalView view = build_goal_view(settings_with(Goal{100, 200, 150, false}), "");
	CHECK_NEAR(view.progress, 0.5, 1e-9);
	CHECK(!view.reached);
}

TEST_CASE(view_is_reached_at_or_above_target)
{
	const GoalView at = build_goal_view(settings_with(Goal{0, 10, 10, true}), "");
	CHECK(at.reached);
	CHECK_NEAR(at.progress, 1.0, 1e-9);

	const GoalView above = build_goal_view(settings_with(Goal{0, 10, 25, true}), "");
	CHECK(above.reached);
	CHECK_EQ(above.value_label, std::string("25 / 10"));
}

TEST_CASE(view_is_not_reached_after_a_drop_below_target)
{
	// The event stays fired, but the bar shows the live value honestly.
	const GoalView view = build_goal_view(settings_with(Goal{0, 10, 8, true}), "");
	CHECK(!view.reached);
}

TEST_CASE(view_appends_provider_status_to_the_value_label)
{
	const GoalView view = build_goal_view(settings_with(Goal{0, 2000, 1234, false}), "", "Offline");
	CHECK_EQ(view.value_label, std::string("1.23K / 2K · Offline"));
	CHECK_NEAR(view.progress, 0.617, 1e-9);
}

TEST_CASE(view_without_status_keeps_the_plain_label)
{
	const GoalView view = build_goal_view(settings_with(Goal{0, 2000, 1234, false}), "", "");
	CHECK_EQ(view.value_label, std::string("1.23K / 2K"));
}

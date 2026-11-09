#include "obs/goal_actions.hpp"

#include "core/goal_settings.hpp"
#include "obs/obs_shutdown.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <string>
#include <vector>

namespace goalbar::obs {
namespace {

constexpr const char *kGroupKey = "goal_actions";
constexpr const char *kCelebrateKey = "celebrate";
constexpr const char *kShowSourceKey = "show_source";
constexpr const char *kShowSecondsKey = "show_seconds";
constexpr const char *kAutoNextKey = "auto_next";
constexpr const char *kTestKey = "test_celebration";
constexpr const char *kMediaEndedSignal = "media_ended";

// Audio and video sources play once and hide when they end; anything else
// (images, text, browser sources) stays up for "Show for (seconds)".
bool is_media_source(obs_source_t *source)
{
	const char *id = obs_source_get_unversioned_id(source);
	const bool player = id && (std::strcmp(id, "ffmpeg_source") == 0 || std::strcmp(id, "vlc_source") == 0);
	return player && (obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA) != 0;
}

// A looping media never sends media_ended. ffmpeg_source calls the setting
// "looping", vlc_source "loop".
bool media_loops(obs_source_t *source)
{
	const char *id = obs_source_get_unversioned_id(source);
	const char *key = id && std::strcmp(id, "vlc_source") == 0 ? "loop" : "looping";
	obs_data_t *settings = obs_source_get_settings(source);
	const bool loops = obs_data_get_bool(settings, key);
	obs_data_release(settings);
	return loops;
}

// --- Scene item visibility, always on the UI thread ---------------------------

bool collect_items(obs_scene_t *, obs_sceneitem_t *item, void *param);

struct ItemSearch {
	obs_source_t *target;
	std::vector<obs_sceneitem_t *> items; // referenced
};

bool collect_items(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *search = static_cast<ItemSearch *>(param);
	if (obs_sceneitem_get_source(item) == search->target) {
		obs_sceneitem_addref(item);
		search->items.push_back(item);
	}
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, collect_items, param);
	return true;
}

bool search_scene(void *param, obs_source_t *scene_source)
{
	if (obs_scene_t *scene = obs_scene_from_source(scene_source))
		obs_scene_enum_items(scene, collect_items, param);
	return true;
}

// Items are collected first and changed after the enumeration, so no scene
// lock is held while visibility signals run.
void set_visible_everywhere(obs_source_t *target, bool visible)
{
	ItemSearch search{target, {}};
	obs_enum_scenes(search_scene, &search);
	for (obs_sceneitem_t *item : search.items) {
		obs_sceneitem_set_visible(item, visible);
		obs_sceneitem_release(item);
	}
}

struct VisibilityTask {
	obs_weak_source_t *target; // owned reference
	bool visible;
	bool restart_media;
};

void run_visibility_task(void *param)
{
	auto *task = static_cast<VisibilityTask *>(param);
	if (obs_source_t *target = obs_weak_source_get_source(task->target)) {
		set_visible_everywhere(target, task->visible);
		if (task->restart_media)
			obs_source_media_restart(target);
		obs_source_release(target);
	}
	obs_weak_source_release(task->target);
	delete task;
}

void queue_visibility(obs_weak_source_t *target, bool visible, bool restart_media)
{
	obs_weak_source_addref(target);
	obs_queue_task(OBS_TASK_UI, run_visibility_task, new VisibilityTask{target, visible, restart_media}, false);
}

// --- Properties ---------------------------------------------------------------

struct SourceList {
	obs_source_t *owner;
	std::vector<std::string> names;
};

bool collect_source_name(void *param, obs_source_t *source)
{
	auto *list = static_cast<SourceList *>(param);
	const char *name = obs_source_get_name(source);
	if (source != list->owner && name && *name)
		list->names.emplace_back(name);
	return true;
}

bool show_source_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool has_source = *obs_data_get_string(settings, kShowSourceKey) != '\0';
	obs_property_set_visible(obs_properties_get(props, kShowSecondsKey), has_source);
	return true;
}

bool auto_next_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_property_set_visible(obs_properties_get(props, settings_key::kStep),
				 obs_data_get_bool(settings, kAutoNextKey));
	return true;
}

void add_source_list(obs_properties_t *group, obs_source_t *owner)
{
	obs_property_t *list = obs_properties_add_list(group, kShowSourceKey, obs_module_text("GoalBar.Actions.ShowSource"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_set_long_description(list, obs_module_text("GoalBar.Actions.ShowSourceHint"));
	obs_property_list_add_string(list, obs_module_text("GoalBar.Actions.None"), "");
	SourceList sources{owner, {}};
	obs_enum_sources(collect_source_name, &sources);
	std::sort(sources.names.begin(), sources.names.end());
	for (const std::string &name : sources.names)
		obs_property_list_add_string(list, name.c_str(), name.c_str());
	obs_property_set_modified_callback(list, show_source_changed);
}

} // namespace

ActionSettings read_action_settings(obs_data_t *settings)
{
	ActionSettings out;
	out.celebrate = obs_data_get_bool(settings, kCelebrateKey);
	out.show_source = obs_data_get_string(settings, kShowSourceKey);
	out.show_seconds = static_cast<int>(std::clamp<long long>(obs_data_get_int(settings, kShowSecondsKey), 1, 120));
	out.auto_next = obs_data_get_bool(settings, kAutoNextKey);
	return out;
}

void set_action_defaults(obs_data_t *settings)
{
	const ActionSettings defaults;
	obs_data_set_default_bool(settings, kCelebrateKey, defaults.celebrate);
	obs_data_set_default_string(settings, kShowSourceKey, defaults.show_source.c_str());
	obs_data_set_default_int(settings, kShowSecondsKey, defaults.show_seconds);
	obs_data_set_default_bool(settings, kAutoNextKey, defaults.auto_next);
}

void add_action_properties(obs_properties_t *props, obs_source_t *owner, obs_property_clicked_t test_clicked,
			   void *data)
{
	obs_properties_t *group = obs_properties_create();
	obs_properties_add_bool(group, kCelebrateKey, obs_module_text("GoalBar.Actions.Celebrate"));
	add_source_list(group, owner);
	obs_properties_add_int(group, kShowSecondsKey, obs_module_text("GoalBar.Actions.ShowSeconds"), 1, 120, 1);
	obs_property_t *auto_next = obs_properties_add_bool(group, kAutoNextKey, obs_module_text("GoalBar.Actions.AutoNext"));
	obs_property_set_modified_callback(auto_next, auto_next_changed);
	obs_properties_add_int(group, settings_key::kStep, obs_module_text("GoalBar.Actions.Step"), 1, INT_MAX, 1);
	obs_properties_add_button2(group, kTestKey, obs_module_text("GoalBar.Actions.Test"), test_clicked, data);
	obs_properties_add_group(props, kGroupKey, obs_module_text("GoalBar.Actions"), OBS_GROUP_NORMAL, group);
}

GoalActions::GoalActions(obs_source_t *owner) : owner_(owner) {}

GoalActions::~GoalActions()
{
	std::lock_guard lock(mutex_);
	// Destroyed mid-celebration: hide the shown source, unless OBS is closing
	// (the UI task may never run, and the scenes are going away anyway).
	if (!obs_shutting_down()) {
		hide();
		return;
	}
	disconnect_media_signal();
	obs_weak_source_release(shown_);
	shown_ = nullptr;
}

void GoalActions::configure(const ActionSettings &settings)
{
	std::lock_guard lock(mutex_);
	settings_ = settings;
}

void GoalActions::trigger(bool test)
{
	std::lock_guard lock(mutex_);
	ActionPlan plan;
	plan.celebrate = settings_.celebrate;
	plan.show_seconds = settings_.show_seconds;
	plan.auto_next = settings_.auto_next;

	obs_source_t *target = nullptr;
	if (!settings_.show_source.empty()) {
		target = obs_get_source_by_name(settings_.show_source.c_str());
		if (target == owner_) {
			obs_source_release(target);
			target = nullptr;
		}
		if (!target)
			obs_log(LOG_WARNING, "goal action: source '%s' not found", settings_.show_source.c_str());
	}
	plan.show_source = target != nullptr;
	plan.source_is_media = target && is_media_source(target);
	plan.source_loops = plan.source_is_media && media_loops(target);

	++seed_;
	media_ended_ = false;
	obs_log(LOG_INFO, "goal actions started%s", test ? " (test)" : "");
	for (const ActionCommand command : sequence_.start(plan, test))
		execute(command, target);
	obs_source_release(target);
}

GoalActions::Tick GoalActions::tick(float seconds)
{
	std::lock_guard lock(mutex_);
	if (sequence_.phase() == ActionPhase::Idle && !was_celebrating_)
		return {};

	std::vector<ActionCommand> commands;
	if (media_ended_.exchange(false))
		commands = sequence_.media_ended();
	for (const ActionCommand command : sequence_.tick(seconds))
		commands.push_back(command);

	Tick result;
	for (const ActionCommand command : commands) {
		if (command == ActionCommand::Advance) {
			result.advance = true;
			sequence_.advance_done(); // the caller advances right after this returns
		} else {
			execute(command, nullptr);
		}
	}

	const bool celebrating = sequence_.celebrating();
	if (celebrating)
		result.frame = celebration_frame(seed_, sequence_.elapsed());
	result.repaint = celebrating || was_celebrating_ || result.advance;
	was_celebrating_ = celebrating;
	return result;
}

// Caller holds mutex_. `target` is only needed for ShowSource.
void GoalActions::execute(ActionCommand command, obs_source_t *target)
{
	switch (command) {
	case ActionCommand::ShowSource:
		if (target)
			show(target);
		break;
	case ActionCommand::HideSource:
		hide();
		break;
	case ActionCommand::Advance:
		break; // done by the caller of tick()
	}
}

void GoalActions::show(obs_source_t *target)
{
	obs_weak_source_t *weak = obs_source_get_weak_source(target);
	if (shown_ && !obs_weak_source_references_source(shown_, target))
		hide(); // a different source is still up from an earlier run
	disconnect_media_signal();
	obs_weak_source_release(shown_);
	shown_ = weak;

	const bool media = is_media_source(target);
	if (media) {
		signal_handler_connect(obs_source_get_signal_handler(target), kMediaEndedSignal, on_media_ended, this);
		media_connected_ = true;
	}
	queue_visibility(shown_, true, media);
}

void GoalActions::hide()
{
	if (!shown_)
		return;
	disconnect_media_signal();
	queue_visibility(shown_, false, false);
	obs_weak_source_release(shown_);
	shown_ = nullptr;
}

// After this returns on_media_ended is not running and never runs again:
// libobs calls signal callbacks under the same lock disconnect takes.
void GoalActions::disconnect_media_signal()
{
	if (!media_connected_)
		return;
	media_connected_ = false;
	if (obs_source_t *source = obs_weak_source_get_source(shown_)) {
		signal_handler_disconnect(obs_source_get_signal_handler(source), kMediaEndedSignal, on_media_ended,
					  this);
		obs_source_release(source);
	}
}

void GoalActions::on_media_ended(void *data, calldata_t *)
{
	static_cast<GoalActions *>(data)->media_ended_ = true;
}

} // namespace goalbar::obs

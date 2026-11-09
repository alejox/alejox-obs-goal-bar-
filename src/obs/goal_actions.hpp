// OBS adapter: executes the goal-reached actions decided by the core
// ActionSequence (built-in celebration, show a scene source, restart media and
// hide it again). Thread-safe: trigger() and tick() may run on any thread.
#pragma once

#include "core/action_sequence.hpp"
#include "core/celebration.hpp"

#include <obs.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace goalbar::obs {

struct ActionSettings {
	bool celebrate = true;
	std::string show_source; // source name, empty for none
	int show_seconds = 8;
	bool auto_next = true;
};

ActionSettings read_action_settings(obs_data_t *settings);
void set_action_defaults(obs_data_t *settings);

// Adds the "When the goal is reached" group. `owner` is left out of the
// source list; `test_clicked` runs the Test celebration button.
void add_action_properties(obs_properties_t *props, obs_source_t *owner, obs_property_clicked_t test_clicked,
			   void *data);

class GoalActions {
public:
	explicit GoalActions(obs_source_t *owner);
	// Disconnects signals and hides a source shown at that moment (skipped
	// while OBS shuts down).
	~GoalActions();

	GoalActions(const GoalActions &) = delete;
	GoalActions &operator=(const GoalActions &) = delete;

	void configure(const ActionSettings &settings);

	// Runs the actions for a reached goal, or for the Test button (`test`),
	// which never advances the goal.
	void trigger(bool test);

	struct Tick {
		std::optional<CelebrationFrame> frame; // set while the animation runs
		bool repaint = false; // the frame changed (also once after it ends)
		bool advance = false; // move to the next goal now
	};

	// Video thread: advances the timers and executes due side effects.
	Tick tick(float seconds);

private:
	void execute(ActionCommand command, obs_source_t *target);
	void show(obs_source_t *target);
	void hide();
	void disconnect_media_signal();
	static void on_media_ended(void *data, calldata_t *params);

	obs_source_t *const owner_;
	std::atomic<bool> media_ended_{false}; // set from the media thread

	std::mutex mutex_;
	ActionSettings settings_;
	ActionSequence sequence_;
	uint32_t seed_ = 0;
	bool was_celebrating_ = false;
	obs_weak_source_t *shown_ = nullptr; // the source made visible, if any
	bool media_connected_ = false;
};

} // namespace goalbar::obs

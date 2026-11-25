// Overlay app: the goal bar's behavior, the same as the plugin's Goal Bar
// source: a metric provider per platform, readings and chat counts applied to
// the goal, the celebration and the automatic next goal. No Qt and no OBS;
// the app calls it from its UI thread and paints what frame() returns.
#pragma once

#include "app/overlay_settings.hpp"
#include "core/action_sequence.hpp"
#include "core/celebration.hpp"
#include "core/chat_events.hpp"
#include "core/goal_reading.hpp"
#include "core/goal_view.hpp"
#include "core/metric_provider.hpp"
#include "core/progress_tween.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace goalbar::app {

// What selects the provider. A change replaces it.
struct ProviderKey {
	Platform platform = Platform::Manual;
	Metric metric = Metric::Likes;
	std::string channel; // canonical YouTube URL, Kick slug or TikTok handle; "" when invalid
	int min_chars = 0;   // comments only

	bool operator==(const ProviderKey &o) const
	{
		return platform == o.platform && metric == o.metric && channel == o.channel && min_chars == o.min_chars;
	}
	bool operator!=(const ProviderKey &o) const { return !(*this == o); }
};

// The provider key the settings ask for (channels normalized).
ProviderKey provider_key_for(const OverlaySettings &settings);

// Status line keys, translated by the app (the plugin's GoalBar.Status.*).
// Empty when live with a value or in manual mode.
std::string status_key(const MetricReading &reading, Platform platform);

class GoalEngine {
public:
	// Null for a key nobody reads (manual mode, no channel).
	using ProviderFactory = std::function<std::unique_ptr<MetricProvider>(const ProviderKey &key)>;

	// Translates a status key for the value label.
	using Translate = std::function<std::string(const std::string &key)>;

	struct Frame {
		GoalView view; // the value label carries the translated status
		std::optional<CelebrationFrame> celebration;
		double style_clock = 0.0; // seconds into the Glitch loop
	};

	explicit GoalEngine(ProviderFactory factory);
	~GoalEngine();

	// Applies what the user set. The goal values in `settings` are the
	// user's: a typed value, target or reset count as edits. Switching to a
	// counted metric starts it at zero (unless `restoring` the saved state at
	// startup). Never celebrates on restore.
	void configure(const OverlaySettings &settings, bool restoring = false);

	// The settings with the runtime goal (what to save).
	const OverlaySettings &settings() const { return settings_; }
	// True once since the last call when the runtime goal changed (save it).
	bool take_goal_changed();

	// Advances everything by `seconds`; true when the frame changed.
	bool tick(double seconds);

	void test_celebration();
	// Re-arms the celebration (and, with auto next, moves a reached goal up).
	void reset_goal();

	Frame frame(const Translate &translate) const;
	// The current status key (see status_key()).
	const std::string &status() const { return status_key_; }

private:
	void replace_provider(const ProviderKey &key);
	bool poll_provider();
	void apply_value(uint64_t value, ReadingKind kind);
	void trigger(bool test);
	void goal_moved();

	ProviderFactory factory_;
	OverlaySettings settings_;
	bool configured_ = false;
	bool goal_changed_ = false;

	std::unique_ptr<MetricProvider> provider_;
	ProviderKey key_;
	std::optional<uint64_t> applied_value_;
	ProviderState applied_state_ = ProviderState::Idle;
	bool needs_baseline_ = true;
	CountCursor count_cursor_;
	std::string status_key_;

	ActionSequence actions_;
	uint32_t seed_ = 0;
	bool was_celebrating_ = false;
	std::optional<CelebrationFrame> celebration_;
	ProgressTween fill_;
	double style_clock_ = 0.0;
	int painted_style_step_ = 0;
};

} // namespace goalbar::app

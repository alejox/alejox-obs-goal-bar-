// Pure core: no OBS, no Qt.
#pragma once

#include <vector>

namespace goalbar {

// Idle → Celebrating (animation and/or shown source) → Advancing (the adapter
// moves to the next goal) → Idle. Test runs and runs without auto-next go
// straight back to Idle.
enum class ActionPhase { Idle, Celebrating, Advancing };

// Side effects the adapter must execute, in order.
enum class ActionCommand { ShowSource, HideSource, Advance };

// A media source is hidden when it ends, but never stays up longer than this.
inline constexpr double kMaxMediaSeconds = 300.0;

struct ActionPlan {
	bool celebrate = true;        // built-in animation, kCelebrationSeconds long
	bool show_source = false;     // a scene source to show while celebrating
	bool source_is_media = false; // hide on media end instead of after show_seconds
	double show_seconds = 8.0;
	bool auto_next = true; // advance to the next goal when everything ends
};

class ActionSequence {
public:
	// Starts the actions for a reached goal, or for a test run (which never
	// advances). Starting again while running restarts the timers and keeps a
	// pending advance.
	std::vector<ActionCommand> start(const ActionPlan &plan, bool test);

	// Advances the timers by `seconds`.
	std::vector<ActionCommand> tick(double seconds);

	// The shown media source reported its end.
	std::vector<ActionCommand> media_ended();

	// The adapter finished the Advance command: back to Idle.
	void advance_done();

	ActionPhase phase() const { return phase_; }
	// True while the built-in animation runs.
	bool celebrating() const;
	// Seconds since start().
	double elapsed() const { return elapsed_; }

private:
	std::vector<ActionCommand> finish_if_done();
	bool source_time_over() const;

	ActionPlan plan_;
	ActionPhase phase_ = ActionPhase::Idle;
	double elapsed_ = 0.0;
	bool source_shown_ = false;
	bool advance_pending_ = false;
};

} // namespace goalbar

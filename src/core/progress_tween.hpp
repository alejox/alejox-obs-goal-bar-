// Pure core: no OBS, no Qt.
#pragma once

namespace goalbar {

// How long the bar fill takes to glide to a new value.
inline constexpr double kFillTweenSeconds = 0.6;

// Ease-out cubic: fast start, gentle landing. `t` is clamped to [0, 1].
double ease_out_cubic(double t);

// The fill fraction shown on screen, gliding toward the goal's progress.
// Values are clamped to [0, 1]. Driven by the caller's frame time.
class ProgressTween {
public:
	explicit ProgressTween(double duration_seconds = kFillTweenSeconds);

	// Shows `value` at once (first paint, reload).
	void jump(double value);

	// Glides from the value shown now to `target`. Same target: no-op.
	void retarget(double target);

	// Advances by `seconds`. Returns true when the shown value changed.
	bool tick(double seconds);

	double value() const { return value_; }
	bool animating() const { return animating_; }

private:
	double duration_;
	double from_ = 0.0;
	double to_ = 0.0;
	double value_ = 0.0;
	double elapsed_ = 0.0;
	bool animating_ = false;
};

} // namespace goalbar

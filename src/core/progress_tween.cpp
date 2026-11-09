#include "core/progress_tween.hpp"

#include <algorithm>

namespace goalbar {
namespace {

double unit(double value)
{
	return std::clamp(value, 0.0, 1.0);
}

} // namespace

double ease_out_cubic(double t)
{
	const double rest = 1.0 - unit(t);
	return 1.0 - rest * rest * rest;
}

ProgressTween::ProgressTween(double duration_seconds) : duration_(duration_seconds) {}

void ProgressTween::jump(double value)
{
	value_ = from_ = to_ = unit(value);
	animating_ = false;
}

void ProgressTween::retarget(double target)
{
	target = unit(target);
	if (target == to_ && (animating_ || target == value_))
		return;
	from_ = value_;
	to_ = target;
	elapsed_ = 0.0;
	animating_ = from_ != to_;
	if (!animating_)
		value_ = to_;
}

bool ProgressTween::tick(double seconds)
{
	if (!animating_)
		return false;
	elapsed_ += seconds;
	const double t = duration_ > 0.0 ? elapsed_ / duration_ : 1.0;
	if (t >= 1.0) {
		value_ = to_;
		animating_ = false;
	} else {
		value_ = from_ + (to_ - from_) * ease_out_cubic(t);
	}
	return true;
}

} // namespace goalbar

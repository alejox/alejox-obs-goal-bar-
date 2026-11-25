#include "app/goal_engine.hpp"

#include "core/kick_api.hpp"
#include "core/style_preset.hpp"
#include "core/subscriber_rounding.hpp"
#include "core/tiktok_live.hpp"
#include "core/youtube_page.hpp"
#include "providers/twitch_provider.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace goalbar::app {
namespace {

bool is_youtube_subscribers(const OverlaySettings &settings)
{
	return settings.platform == Platform::YouTube && settings.metric == Metric::Subscribers;
}

// YouTube rounds subscriber counts: keep the target on a reachable step.
void align_subscriber_target(const OverlaySettings &settings, Goal &goal, uint64_t value)
{
	if (!is_youtube_subscribers(settings))
		return;
	const uint64_t aligned = align_target_to_step(goal.target, value);
	if (aligned != goal.target)
		goal.set_target(aligned);
}

int style_step(const OverlaySettings &settings, double clock)
{
	return style_animates(style_preset_from_id(settings.style)) ? glitch_frame(clock).step : 0;
}

} // namespace

ProviderKey provider_key_for(const OverlaySettings &settings)
{
	ProviderKey key;
	key.platform = settings.platform;
	key.metric = readable_metric(settings.platform, settings.metric);
	switch (settings.platform) {
	case Platform::YouTube:
		key.channel = normalize_youtube_channel(settings.youtube_channel).value_or("");
		break;
	case Platform::Kick:
		key.channel = normalize_kick_slug(settings.kick_channel).value_or("");
		break;
	case Platform::TikTok:
		key.channel = normalize_tiktok_handle(settings.tiktok_channel).value_or("");
		break;
	default:
		break;
	}
	if (key.metric == Metric::Comments)
		key.min_chars = std::clamp(settings.min_chars, 1, kMaxMinChars);
	return key;
}

std::string status_key(const MetricReading &reading, Platform platform)
{
	switch (reading.state) {
	case ProviderState::Live:
		if (reading.value)
			return {};
		return platform == Platform::TikTok ? "GoalBar.Status.WaitingLikes" : "GoalBar.Status.Hidden";
	case ProviderState::Connecting:
		return "GoalBar.Status.Connecting";
	case ProviderState::NotLive:
		return "GoalBar.Status.Offline";
	case ProviderState::AuthRequired:
		return "GoalBar.Status.AuthRequired";
	case ProviderState::RateLimited:
		return reading.message == kTikTokSignLimit ? "GoalBar.Status.TikTokSignLimit" : "GoalBar.Status.RateLimited";
	case ProviderState::Error:
		if (reading.message == kTwitchNeedsAffiliate)
			return "GoalBar.Status.NeedsAffiliate";
		if (reading.message == kKickChannelNotFound)
			return "GoalBar.Status.ChannelNotFound";
		if (reading.message == kKickBlocked)
			return "GoalBar.Status.KickBlocked";
		if (reading.message == kTikTokUserNotFound)
			return "GoalBar.Status.UserNotFound";
		if (reading.message == kTikTokBlocked)
			return "GoalBar.Status.TikTokBlocked";
		return "GoalBar.Status.Error";
	case ProviderState::Idle:
		return {};
	}
	return "GoalBar.Status.Error";
}

GoalEngine::GoalEngine(ProviderFactory factory) : factory_(std::move(factory)) {}

GoalEngine::~GoalEngine() = default;

void GoalEngine::configure(const OverlaySettings &requested, bool restoring)
{
	OverlaySettings next = requested;
	next.metric = readable_metric(next.platform, next.metric);
	const ProviderKey key = provider_key_for(next);

	// Switching to a counted metric starts the count at zero.
	const bool new_count = metric_is_counted(key.metric) &&
			       (key.metric != key_.metric || key.platform != key_.platform);
	if (new_count && configured_ && !restoring) {
		next.goal.current = 0;
		next.goal.start = 0;
		next.goal.reached_fired = false;
	}

	if (is_youtube_subscribers(next)) {
		const uint64_t aligned = align_target_to_step(next.goal.target, next.goal.current);
		next.goal.target = aligned;
	}
	if (configured_ && next.goal.target != settings_.goal.target)
		next.goal.set_target(next.goal.target);
	const GoalEvent event = next.goal.apply(next.goal.current);
	const bool goal_edited = !configured_ || next.goal.current != settings_.goal.current ||
				 next.goal.target != settings_.goal.target || next.goal.start != settings_.goal.start ||
				 next.goal.reached_fired != settings_.goal.reached_fired;

	const bool first = !configured_;
	settings_ = next;
	configured_ = true;
	if (goal_edited && !first)
		goal_changed_ = true;
	if (first)
		fill_.jump(settings_.goal.progress());
	else
		fill_.retarget(settings_.goal.progress());

	if (!provider_ || key != key_ || first)
		replace_provider(key);
	// The first configure restores a saved goal: never celebrate on reload.
	if (event == GoalEvent::GoalReached && !first && !restoring)
		trigger(false);
}

bool GoalEngine::take_goal_changed()
{
	const bool changed = goal_changed_;
	goal_changed_ = false;
	return changed;
}

void GoalEngine::replace_provider(const ProviderKey &key)
{
	provider_.reset(); // joins the old one before the new one starts
	key_ = key;
	if (key.platform != Platform::Manual && factory_)
		provider_ = factory_(key);
	applied_value_.reset();
	applied_state_ = ProviderState::Idle;
	needs_baseline_ = true;
	count_cursor_.reset();
	if (key.platform == Platform::Manual)
		status_key_.clear();
	else
		status_key_ = provider_ ? "GoalBar.Status.Connecting" : "GoalBar.Status.NoChannel";
}

void GoalEngine::goal_moved()
{
	goal_changed_ = true;
	fill_.retarget(settings_.goal.progress());
}

void GoalEngine::apply_value(uint64_t value, ReadingKind kind)
{
	Goal &goal = settings_.goal;
	align_subscriber_target(settings_, goal, value);
	const uint64_t auto_next_step = settings_.auto_next ? settings_.step : 0;
	const ReadingOutcome outcome = apply_metric_value(goal, value, kind, auto_next_step);
	goal_moved();
	if (outcome.event == GoalEvent::GoalReached)
		trigger(false);
}

bool GoalEngine::poll_provider()
{
	if (!provider_)
		return false;
	const MetricReading reading = provider_->latest();
	if (reading.value == applied_value_ && reading.state == applied_state_)
		return false;
	applied_value_ = reading.value;
	applied_state_ = reading.state;
	status_key_ = status_key(reading, key_.platform);
	if (metric_is_counted(key_.metric)) {
		const uint64_t added = reading.value ? count_cursor_.take(*reading.value) : 0;
		if (added > 0) {
			const uint64_t current = settings_.goal.current;
			const uint64_t value = current > UINT64_MAX - added ? UINT64_MAX : current + added;
			apply_value(value, ReadingKind::Update);
		}
		return true;
	}
	if (reading.value) {
		const ReadingKind kind = needs_baseline_ ? ReadingKind::Baseline : ReadingKind::Update;
		needs_baseline_ = false;
		if (kind == ReadingKind::Baseline || *reading.value != settings_.goal.current)
			apply_value(*reading.value, kind);
	}
	return true;
}

void GoalEngine::trigger(bool test)
{
	ActionPlan plan;
	plan.celebrate = settings_.celebrate;
	plan.auto_next = settings_.auto_next;
	++seed_;
	actions_.start(plan, test); // the app has no source to show: only the animation and the advance
}

bool GoalEngine::tick(double seconds)
{
	bool changed = poll_provider();

	bool advance = false;
	if (actions_.phase() != ActionPhase::Idle || was_celebrating_) {
		for (const ActionCommand command : actions_.tick(seconds)) {
			if (command == ActionCommand::Advance) {
				advance = true;
				actions_.advance_done();
			}
		}
		const bool celebrating = actions_.celebrating();
		celebration_.reset();
		if (celebrating)
			celebration_ = celebration_frame(seed_, actions_.elapsed());
		changed = changed || celebrating || was_celebrating_;
		was_celebrating_ = celebrating;
	}
	if (advance) {
		if (std::optional<Goal> next = settings_.goal.advance(settings_.step)) {
			align_subscriber_target(settings_, *next, next->current);
			settings_.goal = *next;
			goal_moved();
			changed = true;
		}
	}

	changed = fill_.tick(seconds) || changed;
	style_clock_ = std::fmod(style_clock_ + seconds, kGlitchPeriodSeconds);
	const int step = style_step(settings_, style_clock_);
	if (step != painted_style_step_) {
		painted_style_step_ = step;
		changed = true;
	}
	return changed;
}

void GoalEngine::test_celebration()
{
	trigger(true);
}

void GoalEngine::reset_goal()
{
	Goal &goal = settings_.goal;
	goalbar::reset_goal(goal, settings_.auto_next ? settings_.step : 0);
	align_subscriber_target(settings_, goal, goal.current);
	goal_moved();
}

GoalEngine::Frame GoalEngine::frame(const Translate &translate) const
{
	GoalSettings goal;
	goal.goal = settings_.goal;
	goal.metric = settings_.metric;
	goal.platform = settings_.platform;
	goal.step = settings_.step;
	Frame out;
	out.view = build_goal_view(goal, settings_.title,
				   status_key_.empty() || !translate ? std::string() : translate(status_key_));
	out.view.progress = fill_.value();
	out.celebration = celebration_;
	out.style_clock = style_clock_;
	return out;
}

} // namespace goalbar::app

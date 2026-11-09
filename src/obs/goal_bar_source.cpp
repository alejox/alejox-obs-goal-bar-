#include "obs/goal_bar_source.hpp"

#include "core/goal_reading.hpp"
#include "core/goal_settings.hpp"
#include "core/goal_view.hpp"
#include "core/style_preset.hpp"
#include "core/subscriber_rounding.hpp"
#include "core/youtube_page.hpp"
#include "obs/goal_actions.hpp"
#include "obs/provider_factory.hpp"
#include "obs/twitch_services.hpp"
#include "providers/twitch_provider.hpp"
#include "render/goal_bar_painter.hpp"
#include "ui/twitch_connect_dialog.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QImage>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace goalbar::obs {
namespace {

constexpr const char *kSourceId = "goal_bar_source";

// Source-only keys; the goal keys come from the core codec (settings_key).
constexpr const char *kTitleKey = "title";
constexpr const char *kStyleKey = "style";
constexpr const char *kAccentKey = "accent_color";
constexpr const char *kTextColorKey = "text_color";
constexpr const char *kWidthKey = "width";
constexpr const char *kHeightKey = "height";
constexpr const char *kResetKey = "reset_goal";
constexpr const char *kChannelKey = "youtube_channel";
constexpr const char *kSubsNoticeKey = "youtube_subs_notice";
constexpr const char *kTwitchStatusKey = "twitch_account_status";
constexpr const char *kTwitchConnectKey = "twitch_connect";
constexpr const char *kTwitchDisconnectKey = "twitch_disconnect";

constexpr long long kDefaultWidth = 800;
constexpr long long kDefaultHeight = 120;
constexpr long long kDefaultAccent = 0xFF6E3DFF; // OBS colors are 0xAABBGGRR
constexpr long long kDefaultTextColor = 0xFFFFFFFF;

constexpr const char *kIntGoalKeys[] = {settings_key::kStart, settings_key::kTarget, settings_key::kCurrent,
					settings_key::kStep};

// What selects the metric provider. A change replaces the provider.
struct ProviderKey {
	Platform platform = Platform::Manual;
	Metric metric = Metric::Likes;
	std::string channel_url; // canonical, empty when invalid

	bool operator==(const ProviderKey &other) const
	{
		return platform == other.platform && metric == other.metric && channel_url == other.channel_url;
	}
};

struct GoalBarSource {
	obs_source_t *source = nullptr;

	// Update side: serializes decode, goal bookkeeping and painting.
	std::mutex update_mutex;
	GoalSettings goal;
	bool has_goal = false;
	std::string title;
	std::string status; // localized provider note, empty when live or manual
	render::BarStyle style;
	bool auto_next = true;
	std::optional<CelebrationFrame> celebration; // drawn while set

	// Goal-reached side effects; outlives every call into this source.
	std::unique_ptr<GoalActions> actions;

	// Provider plus the last reading applied from it (video_tick).
	std::mutex provider_mutex;
	std::unique_ptr<MetricProvider> provider;
	ProviderKey provider_key;
	std::optional<uint64_t> applied_value;
	ProviderState applied_state = ProviderState::Idle;
	bool needs_baseline = true; // the next value is the provider's first one

	// Hand-off to the graphics thread.
	std::mutex image_mutex;
	QImage image;
	bool image_dirty = false;
	std::atomic<uint32_t> width{static_cast<uint32_t>(kDefaultWidth)};
	std::atomic<uint32_t> height{static_cast<uint32_t>(kDefaultHeight)};

	// Graphics thread only.
	gs_texture_t *texture = nullptr;
	uint32_t texture_width = 0;
	uint32_t texture_height = 0;
};

long long to_obs_int(uint64_t value)
{
	return static_cast<long long>(std::min<uint64_t>(value, LLONG_MAX));
}

SettingsMap read_goal_settings(obs_data_t *settings)
{
	SettingsMap values;
	for (const char *key : kIntGoalKeys)
		values[key] = std::to_string(obs_data_get_int(settings, key));
	values[settings_key::kReachedFired] = obs_data_get_bool(settings, settings_key::kReachedFired) ? "1" : "0";
	values[settings_key::kMetric] = obs_data_get_string(settings, settings_key::kMetric);
	values[settings_key::kPlatform] = obs_data_get_string(settings, settings_key::kPlatform);
	return values;
}

QColor color_from_obs(long long value)
{
	const auto color = static_cast<uint32_t>(value);
	return QColor(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, (color >> 24) & 0xFF);
}

render::BarStyle read_style(obs_data_t *settings)
{
	render::BarStyle style;
	style.preset = style_preset_from_id(obs_data_get_string(settings, kStyleKey));
	style.accent = color_from_obs(obs_data_get_int(settings, kAccentKey));
	style.text = color_from_obs(obs_data_get_int(settings, kTextColorKey));
	return style;
}

uint32_t read_dimension(obs_data_t *settings, const char *key, long long fallback)
{
	const long long value = obs_data_get_int(settings, key);
	return static_cast<uint32_t>(std::clamp(value > 0 ? value : fallback, 16LL, 4096LL));
}

bool is_youtube_subscribers(const GoalSettings &settings)
{
	return settings.platform == Platform::YouTube && settings.metric == Metric::Subscribers;
}

std::string status_text(const MetricReading &reading)
{
	switch (reading.state) {
	case ProviderState::Live:
		return reading.value ? std::string() : obs_module_text("GoalBar.Status.Hidden");
	case ProviderState::Connecting:
		return obs_module_text("GoalBar.Status.Connecting");
	case ProviderState::NotLive:
		return obs_module_text("GoalBar.Status.Offline");
	case ProviderState::AuthRequired:
		return obs_module_text("GoalBar.Status.AuthRequired");
	case ProviderState::Error:
		return reading.message == kTwitchNeedsAffiliate ? obs_module_text("GoalBar.Status.NeedsAffiliate")
								 : obs_module_text("GoalBar.Status.Error");
	default:
		return obs_module_text("GoalBar.Status.Error");
	}
}

// Paints the current goal and hands the frame to the graphics thread.
// Caller holds update_mutex.
void repaint_locked(GoalBarSource &self)
{
	const uint32_t width = self.width;
	const uint32_t height = self.height;
	const GoalView view = build_goal_view(self.goal, self.title, self.status);
	QImage image = render::GoalBarPainter::paint(view, self.style, QSize(int(width), int(height)),
						     QString::fromUtf8(obs_module_text("GoalBar.Reached")),
						     self.celebration ? &*self.celebration : nullptr);

	std::lock_guard image_lock(self.image_mutex);
	self.image = std::move(image);
	self.image_dirty = true;
}

void log_if_reached(GoalEvent event, const Goal &goal)
{
	if (event == GoalEvent::GoalReached)
		obs_log(LOG_INFO, "goal reached (%llu / %llu)", static_cast<unsigned long long>(goal.current),
			static_cast<unsigned long long>(goal.target));
}

// Applies goal bookkeeping for a settings change and persists what it changed.
GoalEvent apply_goal_change(GoalBarSource &self, obs_data_t *settings, GoalSettings &next)
{
	const bool persisted_fired = next.goal.reached_fired;
	if (is_youtube_subscribers(next)) {
		// YouTube rounds subscriber counts, so only aligned targets are reachable.
		const uint64_t aligned = align_target_to_step(next.goal.target, next.goal.current);
		if (aligned != next.goal.target) {
			next.goal.target = aligned;
			obs_data_set_int(settings, settings_key::kTarget, to_obs_int(aligned));
		}
	}
	if (self.has_goal && next.goal.target != self.goal.goal.target)
		next.goal.set_target(next.goal.target);

	const GoalEvent event = next.goal.apply(next.goal.current);
	log_if_reached(event, next.goal);

	if (next.goal.reached_fired != persisted_fired)
		obs_data_set_bool(settings, settings_key::kReachedFired, next.goal.reached_fired);
	return event;
}

// Each platform reads only some metrics; an unreadable one falls back to the
// platform's first metric and the choice is persisted.
ProviderKey read_provider_key(obs_data_t *settings)
{
	const GoalSettings decoded = decode_goal_settings(read_goal_settings(settings));
	ProviderKey key{decoded.platform, decoded.metric, {}};
	const bool youtube_followers = key.platform == Platform::YouTube && key.metric == Metric::Followers;
	const bool twitch_likes = key.platform == Platform::Twitch && key.metric == Metric::Likes;
	if (youtube_followers || twitch_likes) {
		key.metric = youtube_followers ? Metric::Likes : Metric::Followers;
		obs_data_set_string(settings, settings_key::kMetric, std::string(metric_id(key.metric)).c_str());
	}
	if (key.platform != Platform::YouTube)
		return key;
	key.channel_url = normalize_youtube_channel(obs_data_get_string(settings, kChannelKey)).value_or("");
	return key;
}

std::string describe_provider(const ProviderKey &key)
{
	switch (key.platform) {
	case Platform::YouTube:
		return key.channel_url.empty() ? "YouTube (no channel)" : key.channel_url;
	case Platform::Twitch:
		return std::string("Twitch ") + std::string(metric_id(key.metric));
	default:
		return "manual";
	}
}

// Swaps in a provider for `key`. Returns the initial status text, or nullopt
// when the key did not change. The old provider is joined outside the lock.
std::optional<std::string> replace_provider(GoalBarSource &self, const ProviderKey &key)
{
	std::unique_ptr<MetricProvider> old;
	std::string status;
	{
		std::lock_guard lock(self.provider_mutex);
		if (self.provider_key == key)
			return std::nullopt;
		std::unique_ptr<MetricProvider> next;
		if (key.platform == Platform::YouTube && !key.channel_url.empty())
			next = make_youtube_provider(key.channel_url, key.metric);
		if (key.platform == Platform::Twitch)
			next = make_twitch_provider(key.metric);
		if (key.platform == Platform::YouTube)
			status = next ? obs_module_text("GoalBar.Status.Connecting")
				      : obs_module_text("GoalBar.Status.NoChannel");
		if (key.platform == Platform::Twitch)
			status = obs_module_text("GoalBar.Status.Connecting");
		old = std::move(self.provider);
		self.provider = std::move(next);
		self.provider_key = key;
		self.applied_value.reset();
		self.applied_state = ProviderState::Idle;
		self.needs_baseline = true;
	}
	if (old || key.platform != Platform::Manual)
		obs_log(LOG_INFO, "metric provider: %s", describe_provider(key).c_str());
	return status;
}

void goal_bar_update(void *data, obs_data_t *settings)
{
	auto *self = static_cast<GoalBarSource *>(data);
	const std::optional<std::string> provider_status = replace_provider(*self, read_provider_key(settings));

	std::lock_guard update_lock(self->update_mutex);
	GoalSettings next = decode_goal_settings(read_goal_settings(settings));
	const GoalEvent event = apply_goal_change(*self, settings, next);
	const ActionSettings actions = read_action_settings(settings);
	self->actions->configure(actions);
	self->auto_next = actions.auto_next;
	// The first update restores a saved goal: never celebrate on reload.
	if (event == GoalEvent::GoalReached && self->has_goal)
		self->actions->trigger(false);
	self->goal = next;
	self->has_goal = true;
	if (provider_status)
		self->status = *provider_status;
	self->title = obs_data_get_string(settings, kTitleKey);
	self->style = read_style(settings);
	self->width = read_dimension(settings, kWidthKey, kDefaultWidth);
	self->height = read_dimension(settings, kHeightKey, kDefaultHeight);
	repaint_locked(*self);
}

// Persists the values a provider reading changed. Never calls obs_source_update.
void persist_goal(obs_source_t *source, const Goal &goal)
{
	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_int(settings, settings_key::kStart, to_obs_int(goal.start));
	obs_data_set_int(settings, settings_key::kCurrent, to_obs_int(goal.current));
	obs_data_set_int(settings, settings_key::kTarget, to_obs_int(goal.target));
	obs_data_set_bool(settings, settings_key::kReachedFired, goal.reached_fired);
	obs_data_release(settings);
}

// YouTube rounds subscriber counts: keep the target on a reachable step.
// Re-arms only when the aligned target moves above the value.
void align_subscriber_target(const GoalSettings &settings, Goal &goal, uint64_t value)
{
	if (!is_youtube_subscribers(settings))
		return;
	const uint64_t aligned = align_target_to_step(goal.target, value);
	if (aligned != goal.target)
		goal.set_target(aligned);
}

void apply_reading(GoalBarSource &self, const MetricReading &reading, ReadingKind kind)
{
	std::lock_guard update_lock(self.update_mutex);
	self.status = status_text(reading);
	const bool has_new_value =
		reading.value && (kind == ReadingKind::Baseline || *reading.value != self.goal.goal.current);
	if (has_new_value) {
		Goal &goal = self.goal.goal;
		align_subscriber_target(self.goal, goal, *reading.value);
		const uint64_t auto_next_step = self.auto_next ? self.goal.step : 0;
		const ReadingOutcome outcome = apply_metric_value(goal, *reading.value, kind, auto_next_step);
		log_if_reached(outcome.event, goal);
		persist_goal(self.source, goal);
		if (outcome.event == GoalEvent::GoalReached)
			self.actions->trigger(false);
	}
	repaint_locked(self);
}

// Cheap check for a new provider reading.
void poll_provider(GoalBarSource &self)
{
	MetricReading reading;
	ReadingKind kind = ReadingKind::Update;
	{
		std::lock_guard lock(self.provider_mutex);
		if (!self.provider)
			return;
		reading = self.provider->latest();
		if (reading.value == self.applied_value && reading.state == self.applied_state)
			return;
		self.applied_value = reading.value;
		self.applied_state = reading.state;
		if (reading.value && self.needs_baseline) {
			kind = ReadingKind::Baseline;
			self.needs_baseline = false;
		}
	}
	apply_reading(self, reading, kind);
}

// Moves to the next goal after a celebration. Caller holds update_mutex.
void advance_goal_locked(GoalBarSource &self)
{
	std::optional<Goal> next = self.goal.goal.advance(self.goal.step);
	if (!next)
		return;
	align_subscriber_target(self.goal, *next, next->current);
	self.goal.goal = *next;
	persist_goal(self.source, self.goal.goal);
	obs_log(LOG_INFO, "next goal: %llu", static_cast<unsigned long long>(next->target));
}

// Advances the goal actions; repaints only while celebrating.
void run_actions(GoalBarSource &self, float seconds)
{
	const GoalActions::Tick tick = self.actions->tick(seconds);
	if (!tick.repaint && !tick.advance)
		return;
	std::lock_guard update_lock(self.update_mutex);
	self.celebration = tick.frame;
	if (tick.advance)
		advance_goal_locked(self);
	repaint_locked(self);
}

// Video thread.
void goal_bar_tick(void *data, float seconds)
{
	auto *self = static_cast<GoalBarSource *>(data);
	poll_provider(*self);
	run_actions(*self, seconds);
}

// Graphics thread: uploads the latest painted frame, if any.
void upload_if_dirty(GoalBarSource &self)
{
	std::lock_guard image_lock(self.image_mutex);
	if (!self.image_dirty || self.image.isNull())
		return;
	self.image_dirty = false;

	const auto width = static_cast<uint32_t>(self.image.width());
	const auto height = static_cast<uint32_t>(self.image.height());
	const uint8_t *bits = self.image.constBits();
	const auto line_size = static_cast<uint32_t>(self.image.bytesPerLine());

	if (self.texture && self.texture_width == width && self.texture_height == height) {
		gs_texture_set_image(self.texture, bits, line_size, false);
		return;
	}
	gs_texture_destroy(self.texture);
	// QImage ARGB32 is 0xAARRGGBB per pixel: B, G, R, A in memory on little-endian targets.
	self.texture = gs_texture_create(width, height, GS_BGRA, 1, &bits, GS_DYNAMIC);
	self.texture_width = self.texture ? width : 0;
	self.texture_height = self.texture ? height : 0;
}

void goal_bar_render(void *data, gs_effect_t *effect)
{
	auto *self = static_cast<GoalBarSource *>(data);
	upload_if_dirty(*self);
	if (!self->texture)
		return;

	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_blend_state_push();
	// The painted image has premultiplied alpha.
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), self->texture);
	gs_draw_sprite(self->texture, 0, self->texture_width, self->texture_height);

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous_srgb);
}

bool reset_goal_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	auto *self = static_cast<GoalBarSource *>(data);
	if (!self) // properties requested without a source instance
		return false;
	uint64_t current = 0;
	{
		std::lock_guard update_lock(self->update_mutex);
		Goal goal = self->goal.goal;
		goal.reset();
		current = goal.start;
	}
	obs_data_t *changes = obs_data_create();
	obs_data_set_int(changes, settings_key::kStart, to_obs_int(current));
	obs_data_set_bool(changes, settings_key::kReachedFired, false);
	obs_source_update(self->source, changes);
	obs_data_release(changes);
	return false;
}

bool test_celebration_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	auto *self = static_cast<GoalBarSource *>(data);
	if (self) // properties requested without a source instance
		self->actions->trigger(true);
	return false;
}

const char *goal_bar_name(void *)
{
	return obs_module_text("GoalBar");
}

void *goal_bar_create(obs_data_t *settings, obs_source_t *source)
{
	auto *self = new GoalBarSource();
	self->source = source;
	self->actions = std::make_unique<GoalActions>(source);
	goal_bar_update(self, settings);
	return self;
}

void goal_bar_destroy(void *data)
{
	auto *self = static_cast<GoalBarSource *>(data);
	{
		// Join the provider's worker before anything else goes away.
		std::unique_ptr<MetricProvider> provider;
		std::lock_guard lock(self->provider_mutex);
		provider = std::move(self->provider);
	}
	self->actions.reset(); // disconnects its signals
	obs_enter_graphics();
	gs_texture_destroy(self->texture);
	obs_leave_graphics();
	delete self;
}

uint32_t goal_bar_width(void *data)
{
	return static_cast<GoalBarSource *>(data)->width;
}

uint32_t goal_bar_height(void *data)
{
	return static_cast<GoalBarSource *>(data)->height;
}

void goal_bar_defaults(obs_data_t *settings)
{
	const GoalSettings defaults;
	obs_data_set_default_string(settings, kTitleKey, obs_module_text("GoalBar.DefaultTitle"));
	obs_data_set_default_int(settings, settings_key::kStart, to_obs_int(defaults.goal.start));
	obs_data_set_default_int(settings, settings_key::kTarget, to_obs_int(defaults.goal.target));
	obs_data_set_default_int(settings, settings_key::kCurrent, to_obs_int(defaults.goal.current));
	obs_data_set_default_int(settings, settings_key::kStep, to_obs_int(defaults.step));
	obs_data_set_default_bool(settings, settings_key::kReachedFired, false);
	obs_data_set_default_string(settings, settings_key::kMetric, std::string(metric_id(defaults.metric)).c_str());
	obs_data_set_default_string(settings, settings_key::kPlatform,
				    std::string(platform_id(defaults.platform)).c_str());
	obs_data_set_default_string(settings, kChannelKey, "");
	obs_data_set_default_string(settings, kStyleKey, std::string(style_preset_id(StylePreset::Minimal)).c_str());
	obs_data_set_default_int(settings, kAccentKey, kDefaultAccent);
	obs_data_set_default_int(settings, kTextColorKey, kDefaultTextColor);
	obs_data_set_default_int(settings, kWidthKey, kDefaultWidth);
	obs_data_set_default_int(settings, kHeightKey, kDefaultHeight);
	set_action_defaults(settings);
}

Platform selected_platform(obs_data_t *settings)
{
	return platform_from_id(obs_data_get_string(settings, settings_key::kPlatform)).value_or(Platform::Manual);
}

// Fills the metric list with the metrics `platform` can read.
void fill_metric_list(obs_property_t *list, Platform platform)
{
	obs_property_list_clear(list);
	const std::pair<Metric, const char *> metrics[] = {{Metric::Likes, "Metric.Likes"},
							   {Metric::Subscribers, "Metric.Subscribers"},
							   {Metric::Followers, "Metric.Followers"}};
	for (const auto &[metric, label] : metrics) {
		const bool unreadable = (platform == Platform::YouTube && metric == Metric::Followers) ||
					(platform == Platform::Twitch && metric == Metric::Likes);
		if (!unreadable)
			obs_property_list_add_string(list, obs_module_text(label),
						     std::string(metric_id(metric)).c_str());
	}
}

// Shows the Twitch account line and the matching Connect/Disconnect button.
// Reads the account (and so the secret store) only when Twitch is selected.
void update_twitch_account(obs_properties_t *props, bool twitch)
{
	obs_property_t *status = obs_properties_get(props, kTwitchStatusKey);
	obs_property_t *connect = obs_properties_get(props, kTwitchConnectKey);
	obs_property_t *disconnect = obs_properties_get(props, kTwitchDisconnectKey);
	bool connected = false;
	if (twitch) {
		const TwitchAccount::Status account = twitch_account()->status();
		connected = account.connected;
		std::string line = obs_module_text("GoalBar.Twitch.NotConnected");
		if (connected) {
			line = obs_module_text("GoalBar.Twitch.Connected");
			if (!account.display_name.empty())
				line = obs_module_text("GoalBar.Twitch.ConnectedAs") + std::string(" ") +
				       account.display_name;
		}
		obs_property_set_description(status, line.c_str());
	}
	obs_property_set_visible(status, twitch);
	obs_property_set_visible(connect, twitch && !connected);
	obs_property_set_visible(disconnect, twitch && connected);
}

bool twitch_connect_clicked(obs_properties_t *props, obs_property_t *, void *)
{
	auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	ui::run_twitch_connect_dialog(parent, twitch_account());
	update_twitch_account(props, true);
	return true;
}

bool twitch_disconnect_clicked(obs_properties_t *props, obs_property_t *, void *)
{
	twitch_account()->disconnect();
	update_twitch_account(props, true);
	return true;
}

bool metric_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool subs = metric_from_id(obs_data_get_string(settings, settings_key::kMetric)) == Metric::Subscribers;
	obs_property_set_visible(obs_properties_get(props, kSubsNoticeKey),
				 subs && selected_platform(settings) == Platform::YouTube);
	return true;
}

bool platform_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	const Platform platform = selected_platform(settings);
	obs_property_set_visible(obs_properties_get(props, kChannelKey), platform == Platform::YouTube);
	obs_property_set_visible(obs_properties_get(props, settings_key::kCurrent), platform == Platform::Manual);
	update_twitch_account(props, platform == Platform::Twitch);
	fill_metric_list(obs_properties_get(props, settings_key::kMetric), selected_platform(settings));
	return metric_changed(props, property, settings);
}

void add_platform_list(obs_properties_t *props)
{
	obs_property_t *list = obs_properties_add_list(props, settings_key::kPlatform,
						       obs_module_text("GoalBar.Platform"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	const std::pair<Platform, const char *> platforms[] = {{Platform::Manual, "Platform.Manual"},
							       {Platform::YouTube, "Platform.YouTube"},
							       {Platform::Twitch, "Platform.Twitch"}};
	for (const auto &[platform, label] : platforms)
		obs_property_list_add_string(list, obs_module_text(label), std::string(platform_id(platform)).c_str());
	obs_property_set_modified_callback(list, platform_changed);
}

void add_metric_list(obs_properties_t *props)
{
	obs_property_t *list = obs_properties_add_list(props, settings_key::kMetric, obs_module_text("GoalBar.Metric"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	fill_metric_list(list, Platform::Manual);
	obs_property_set_modified_callback(list, metric_changed);
}

void add_style_list(obs_properties_t *props)
{
	obs_property_t *list = obs_properties_add_list(props, kStyleKey, obs_module_text("GoalBar.Style"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	const std::pair<StylePreset, const char *> styles[] = {{StylePreset::Minimal, "Style.Minimal"},
							       {StylePreset::Bold, "Style.Bold"},
							       {StylePreset::Neon, "Style.Neon"}};
	for (const auto &[preset, label] : styles)
		obs_property_list_add_string(list, obs_module_text(label), std::string(style_preset_id(preset)).c_str());
}

obs_properties_t *goal_bar_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	add_platform_list(props);
	obs_properties_add_text(props, kChannelKey, obs_module_text("GoalBar.Channel"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, kTwitchStatusKey, obs_module_text("GoalBar.Twitch.NotConnected"), OBS_TEXT_INFO);
	obs_properties_add_button2(props, kTwitchConnectKey, obs_module_text("GoalBar.Twitch.Connect"),
				   twitch_connect_clicked, nullptr);
	obs_properties_add_button2(props, kTwitchDisconnectKey, obs_module_text("GoalBar.Twitch.Disconnect"),
				   twitch_disconnect_clicked, nullptr);
	obs_properties_add_text(props, kTitleKey, obs_module_text("GoalBar.Title"), OBS_TEXT_DEFAULT);
	add_metric_list(props);
	obs_properties_add_text(props, kSubsNoticeKey, obs_module_text("GoalBar.SubsNotice"), OBS_TEXT_INFO);
	obs_properties_add_int(props, settings_key::kCurrent, obs_module_text("GoalBar.Current"), 0, INT_MAX, 1);
	obs_properties_add_int(props, settings_key::kTarget, obs_module_text("GoalBar.Target"), 1, INT_MAX, 1);
	add_style_list(props);
	obs_properties_add_color_alpha(props, kAccentKey, obs_module_text("GoalBar.AccentColor"));
	obs_properties_add_color_alpha(props, kTextColorKey, obs_module_text("GoalBar.TextColor"));
	obs_properties_add_int(props, kWidthKey, obs_module_text("GoalBar.Width"), 16, 4096, 1);
	obs_properties_add_int(props, kHeightKey, obs_module_text("GoalBar.Height"), 16, 4096, 1);
	obs_properties_add_button2(props, kResetKey, obs_module_text("GoalBar.ResetGoal"), reset_goal_clicked, data);
	auto *self = static_cast<GoalBarSource *>(data);
	add_action_properties(props, self ? self->source : nullptr, test_celebration_clicked, data);
	return props;
}

} // namespace

void register_goal_bar_source()
{
	obs_source_info info = {};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	info.get_name = goal_bar_name;
	info.create = goal_bar_create;
	info.destroy = goal_bar_destroy;
	info.update = goal_bar_update;
	info.get_defaults = goal_bar_defaults;
	info.get_properties = goal_bar_properties;
	info.get_width = goal_bar_width;
	info.get_height = goal_bar_height;
	info.video_tick = goal_bar_tick;
	info.video_render = goal_bar_render;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}

} // namespace goalbar::obs

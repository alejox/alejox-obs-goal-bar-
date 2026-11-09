#include "obs/goal_bar_source.hpp"

#include "core/goal_settings.hpp"
#include "core/goal_view.hpp"
#include "core/style_preset.hpp"
#include "render/goal_bar_painter.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QImage>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <mutex>
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

constexpr long long kDefaultWidth = 800;
constexpr long long kDefaultHeight = 120;
constexpr long long kDefaultAccent = 0xFF6E3DFF; // OBS colors are 0xAABBGGRR
constexpr long long kDefaultTextColor = 0xFFFFFFFF;

constexpr const char *kIntGoalKeys[] = {settings_key::kStart, settings_key::kTarget, settings_key::kCurrent,
					settings_key::kStep};

struct GoalBarSource {
	obs_source_t *source = nullptr;

	// Update side: serializes decode, goal bookkeeping and painting.
	std::mutex update_mutex;
	GoalSettings goal;
	bool has_goal = false;

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

// Applies goal bookkeeping for a settings change and persists the fired flag.
void apply_goal_change(GoalBarSource &self, obs_data_t *settings, GoalSettings &next)
{
	const bool persisted_fired = next.goal.reached_fired;
	if (self.has_goal && next.goal.target != self.goal.goal.target)
		next.goal.set_target(next.goal.target);

	if (next.goal.apply(next.goal.current) == GoalEvent::GoalReached)
		obs_log(LOG_INFO, "goal reached (%llu / %llu)", static_cast<unsigned long long>(next.goal.current),
			static_cast<unsigned long long>(next.goal.target));

	if (next.goal.reached_fired != persisted_fired)
		obs_data_set_bool(settings, settings_key::kReachedFired, next.goal.reached_fired);
}

void goal_bar_update(void *data, obs_data_t *settings)
{
	auto *self = static_cast<GoalBarSource *>(data);
	std::lock_guard update_lock(self->update_mutex);

	GoalSettings next = decode_goal_settings(read_goal_settings(settings));
	apply_goal_change(*self, settings, next);
	self->goal = next;
	self->has_goal = true;

	const uint32_t width = read_dimension(settings, kWidthKey, kDefaultWidth);
	const uint32_t height = read_dimension(settings, kHeightKey, kDefaultHeight);
	const GoalView view = build_goal_view(next, obs_data_get_string(settings, kTitleKey));
	QImage image = render::GoalBarPainter::paint(view, read_style(settings), QSize(int(width), int(height)),
						     QString::fromUtf8(obs_module_text("GoalBar.Reached")));

	std::lock_guard image_lock(self->image_mutex);
	self->image = std::move(image);
	self->image_dirty = true;
	self->width = width;
	self->height = height;
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

const char *goal_bar_name(void *)
{
	return obs_module_text("GoalBar");
}

void *goal_bar_create(obs_data_t *settings, obs_source_t *source)
{
	auto *self = new GoalBarSource();
	self->source = source;
	goal_bar_update(self, settings);
	return self;
}

void goal_bar_destroy(void *data)
{
	auto *self = static_cast<GoalBarSource *>(data);
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
	obs_data_set_default_string(settings, kStyleKey, std::string(style_preset_id(StylePreset::Minimal)).c_str());
	obs_data_set_default_int(settings, kAccentKey, kDefaultAccent);
	obs_data_set_default_int(settings, kTextColorKey, kDefaultTextColor);
	obs_data_set_default_int(settings, kWidthKey, kDefaultWidth);
	obs_data_set_default_int(settings, kHeightKey, kDefaultHeight);
}

void add_metric_list(obs_properties_t *props)
{
	obs_property_t *list = obs_properties_add_list(props, settings_key::kMetric, obs_module_text("GoalBar.Metric"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	const std::pair<Metric, const char *> metrics[] = {{Metric::Likes, "Metric.Likes"},
							   {Metric::Subscribers, "Metric.Subscribers"},
							   {Metric::Followers, "Metric.Followers"}};
	for (const auto &[metric, label] : metrics)
		obs_property_list_add_string(list, obs_module_text(label), std::string(metric_id(metric)).c_str());
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
	obs_properties_add_text(props, kTitleKey, obs_module_text("GoalBar.Title"), OBS_TEXT_DEFAULT);
	add_metric_list(props);
	obs_properties_add_int(props, settings_key::kCurrent, obs_module_text("GoalBar.Current"), 0, INT_MAX, 1);
	obs_properties_add_int(props, settings_key::kTarget, obs_module_text("GoalBar.Target"), 1, INT_MAX, 1);
	add_style_list(props);
	obs_properties_add_color_alpha(props, kAccentKey, obs_module_text("GoalBar.AccentColor"));
	obs_properties_add_color_alpha(props, kTextColorKey, obs_module_text("GoalBar.TextColor"));
	obs_properties_add_int(props, kWidthKey, obs_module_text("GoalBar.Width"), 16, 4096, 1);
	obs_properties_add_int(props, kHeightKey, obs_module_text("GoalBar.Height"), 16, 4096, 1);
	obs_properties_add_button2(props, kResetKey, obs_module_text("GoalBar.ResetGoal"), reset_goal_clicked, data);
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
	info.video_render = goal_bar_render;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}

} // namespace goalbar::obs

#include "app/overlay_settings.hpp"

#include "core/style_preset.hpp"

#include <json.hpp>

#include <algorithm>

namespace goalbar::app {
namespace {

using nlohmann::json;

template<typename T> T number_or(const json &object, const char *key, T fallback, T low, T high)
{
	const auto it = object.find(key);
	if (it == object.end() || !it->is_number())
		return fallback;
	if (it->is_number_unsigned()) {
		const uint64_t value = it->get<uint64_t>();
		return value > uint64_t(high) ? high : std::max<T>(low, T(value));
	}
	if (it->is_number_integer()) {
		const long long value = it->get<long long>();
		if (value < 0)
			return low;
		if (uint64_t(value) > uint64_t(high))
			return high;
		return std::clamp<T>(T(value), low, high);
	}
	return fallback; // floating point values are not expected
}

std::string string_or(const json &object, const char *key, const std::string &fallback)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

bool bool_or(const json &object, const char *key, bool fallback)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

} // namespace

bool OverlaySettings::operator==(const OverlaySettings &o) const
{
	return platform == o.platform && metric == o.metric && youtube_channel == o.youtube_channel &&
	       kick_channel == o.kick_channel && tiktok_channel == o.tiktok_channel && title == o.title &&
	       min_chars == o.min_chars && goal.start == o.goal.start && goal.target == o.goal.target &&
	       goal.current == o.goal.current && goal.reached_fired == o.goal.reached_fired && step == o.step &&
	       celebrate == o.celebrate && auto_next == o.auto_next && style == o.style && accent == o.accent &&
	       text_color == o.text_color && opacity == o.opacity && width == o.width && height == o.height &&
	       port == o.port && language == o.language;
}

std::string settings_to_json(const OverlaySettings &s)
{
	json out = {
		{"platform", std::string(platform_id(s.platform))},
		{"metric", std::string(metric_id(s.metric))},
		{"youtube_channel", s.youtube_channel},
		{"kick_channel", s.kick_channel},
		{"tiktok_channel", s.tiktok_channel},
		{"title", s.title},
		{"chat_min_chars", s.min_chars},
		{"goal_start", s.goal.start},
		{"goal_target", s.goal.target},
		{"goal_current", s.goal.current},
		{"goal_reached_fired", s.goal.reached_fired},
		{"goal_step", s.step},
		{"celebrate", s.celebrate},
		{"auto_next", s.auto_next},
		{"style", s.style},
		{"accent_color", s.accent},
		{"text_color", s.text_color},
		{"opacity", s.opacity},
		{"width", s.width},
		{"height", s.height},
		{"port", s.port},
		{"language", s.language},
	};
	return out.dump(2);
}

OverlaySettings settings_from_json(const std::string &text)
{
	OverlaySettings s;
	const json parsed = json::parse(text, nullptr, false);
	if (!parsed.is_object())
		return s;
	s.platform = platform_from_id(string_or(parsed, "platform", "")).value_or(s.platform);
	s.metric = readable_metric(s.platform, metric_from_id(string_or(parsed, "metric", "")).value_or(s.metric));
	s.youtube_channel = string_or(parsed, "youtube_channel", "");
	s.kick_channel = string_or(parsed, "kick_channel", "");
	s.tiktok_channel = string_or(parsed, "tiktok_channel", "");
	s.title = string_or(parsed, "title", "");
	s.min_chars = number_or<int>(parsed, "chat_min_chars", kDefaultMinChars, 1, kMaxMinChars);

	s.goal.start = number_or<uint64_t>(parsed, "goal_start", 0, 0, UINT64_MAX);
	s.goal.target = number_or<uint64_t>(parsed, "goal_target", kDefaultGoalTarget, 1, UINT64_MAX);
	s.goal.current = number_or<uint64_t>(parsed, "goal_current", 0, 0, UINT64_MAX);
	s.goal.reached_fired = bool_or(parsed, "goal_reached_fired", false);
	if (s.goal.start > s.goal.current)
		s.goal.start = 0;
	s.step = number_or<uint64_t>(parsed, "goal_step", kDefaultGoalStep, 1, UINT64_MAX);
	s.celebrate = bool_or(parsed, "celebrate", true);
	s.auto_next = bool_or(parsed, "auto_next", true);

	s.style = std::string(style_preset_id(style_preset_from_id(string_or(parsed, "style", s.style))));
	s.accent = number_or<uint32_t>(parsed, "accent_color", s.accent, 0, UINT32_MAX);
	s.text_color = number_or<uint32_t>(parsed, "text_color", s.text_color, 0, UINT32_MAX);
	s.opacity = number_or<int>(parsed, "opacity", s.opacity, 0, 100);
	s.width = number_or<uint32_t>(parsed, "width", s.width, 16, 4096);
	s.height = number_or<uint32_t>(parsed, "height", s.height, 16, 4096);
	s.port = number_or<uint16_t>(parsed, "port", kDefaultOverlayPort, 1024, 65535);
	const std::string language = string_or(parsed, "language", "");
	s.language = language == "es" || language == "en" ? language : "";
	return s;
}

} // namespace goalbar::app

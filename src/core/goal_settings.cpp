#include "core/goal_settings.hpp"

#include <charconv>
#include <optional>
#include <string_view>

namespace goalbar {
namespace {

constexpr const char *kStartKey = settings_key::kStart;
constexpr const char *kTargetKey = settings_key::kTarget;
constexpr const char *kCurrentKey = settings_key::kCurrent;
constexpr const char *kReachedFiredKey = settings_key::kReachedFired;
constexpr const char *kMetricKey = settings_key::kMetric;
constexpr const char *kPlatformKey = settings_key::kPlatform;
constexpr const char *kStepKey = settings_key::kStep;

std::optional<std::string_view> find(const SettingsMap &values, const char *key)
{
	const auto it = values.find(key);
	if (it == values.end())
		return std::nullopt;
	return std::string_view(it->second);
}

// Accepts only a complete, non-negative decimal number that fits in uint64.
std::optional<uint64_t> parse_uint(std::optional<std::string_view> text)
{
	if (!text || text->empty())
		return std::nullopt;
	uint64_t value = 0;
	const char *end = text->data() + text->size();
	const auto [ptr, error] = std::from_chars(text->data(), end, value);
	if (error != std::errc() || ptr != end)
		return std::nullopt;
	return value;
}

std::optional<bool> parse_bool(std::optional<std::string_view> text)
{
	if (text == "1" || text == "true")
		return true;
	if (text == "0" || text == "false")
		return false;
	return std::nullopt;
}

} // namespace

SettingsMap encode_goal_settings(const GoalSettings &settings)
{
	return {
		{kStartKey, std::to_string(settings.goal.start)},
		{kTargetKey, std::to_string(settings.goal.target)},
		{kCurrentKey, std::to_string(settings.goal.current)},
		{kReachedFiredKey, settings.goal.reached_fired ? "1" : "0"},
		{kMetricKey, std::string(metric_id(settings.metric))},
		{kPlatformKey, std::string(platform_id(settings.platform))},
		{kStepKey, std::to_string(settings.step)},
	};
}

GoalSettings decode_goal_settings(const SettingsMap &values)
{
	GoalSettings settings;

	settings.goal.start = parse_uint(find(values, kStartKey)).value_or(settings.goal.start);
	settings.goal.current = parse_uint(find(values, kCurrentKey)).value_or(settings.goal.current);
	settings.goal.reached_fired =
		parse_bool(find(values, kReachedFiredKey)).value_or(settings.goal.reached_fired);

	// Target and step must be positive; zero is as invalid as garbage.
	if (const auto target = parse_uint(find(values, kTargetKey)); target && *target > 0)
		settings.goal.target = *target;
	if (const auto step = parse_uint(find(values, kStepKey)); step && *step > 0)
		settings.step = *step;

	if (const auto id = find(values, kMetricKey))
		settings.metric = metric_from_id(*id).value_or(settings.metric);
	if (const auto id = find(values, kPlatformKey))
		settings.platform = platform_from_id(*id).value_or(settings.platform);

	return settings;
}

} // namespace goalbar

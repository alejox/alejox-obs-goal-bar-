// Overlay app: everything the user sets, the same fields as the plugin's
// Goal Bar source, kept as a JSON file in the app's config folder.
#pragma once

#include "core/goal.hpp"
#include "core/goal_settings.hpp"
#include "core/metric.hpp"

#include <cstdint>
#include <string>

namespace goalbar::app {

inline constexpr uint16_t kDefaultOverlayPort = 48722;
inline constexpr int kDefaultMinChars = 5;
inline constexpr int kMaxMinChars = 500;

struct OverlaySettings {
	Platform platform = Platform::Manual;
	Metric metric = Metric::Likes;
	std::string youtube_channel; // as typed (@handle or URL)
	std::string kick_channel;
	std::string tiktok_channel;
	std::string title;
	int min_chars = kDefaultMinChars; // comments only

	Goal goal{0, kDefaultGoalTarget, 0, false};
	uint64_t step = kDefaultGoalStep; // auto next goal: how much the goal moves up
	bool celebrate = true;
	bool auto_next = true;

	std::string style = "minimal"; // a style_preset_id
	uint32_t accent = 0xFFFF3D6E;  // 0xAARRGGBB
	uint32_t text_color = 0xFFFFFFFF;
	int opacity = 85; // card background, 0–100
	uint32_t width = 800;
	uint32_t height = 120;

	uint16_t port = kDefaultOverlayPort;
	std::string language; // "" follows the system; "es" or "en"

	bool operator==(const OverlaySettings &other) const;
	bool operator!=(const OverlaySettings &other) const { return !(*this == other); }
};

// The settings as JSON text, and back. Unknown or invalid values fall back
// to the defaults one by one; a metric the platform cannot read becomes the
// platform's first metric (see readable_metric).
std::string settings_to_json(const OverlaySettings &settings);
OverlaySettings settings_from_json(const std::string &json);

} // namespace goalbar::app

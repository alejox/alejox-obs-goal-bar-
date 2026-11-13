// Pure core: no OBS, no Qt.
#pragma once

#include <string_view>

namespace goalbar {

// Minimal, Bold and Neon are the original bar-only looks. The others are the
// card styles shared with multi-chat and music-now-playing (same ids).
enum class StylePreset { Minimal, Bold, Neon, Glass, Pill, MacOS, Line, Synthwave, NeonCity, Cyber, Retro, Glitch };

// In the order the properties list shows them.
inline constexpr StylePreset kStylePresets[] = {
	StylePreset::Minimal, StylePreset::Bold, StylePreset::Neon, StylePreset::Glass,
	StylePreset::Pill, StylePreset::MacOS, StylePreset::Line, StylePreset::Synthwave,
	StylePreset::NeonCity, StylePreset::Cyber, StylePreset::Retro, StylePreset::Glitch};

// Stable ids used for settings persistence. Never change an existing id.
std::string_view style_preset_id(StylePreset preset);

// Unknown ids fall back to Minimal, so a stale setting still renders.
StylePreset style_preset_from_id(std::string_view id);

// Card styles draw a background panel behind the title and the bar, and
// honor the background opacity setting.
bool style_has_card(StylePreset preset);

// Whether the accent and text colors apply. Themed styles bring their own.
bool style_uses_custom_colors(StylePreset preset);

// Whether the style moves on its own (and needs periodic repaints).
bool style_animates(StylePreset preset);

// Glitch / VHS: the box holds still, then jumps sideways and shows only a
// horizontal band for a few frames, like the CSS keyframes in the other plugins.
inline constexpr double kGlitchPeriodSeconds = 4.0;

struct GlitchFrame {
	int step = 0;             // 0 still; 1..3 the jumps, for change detection
	double shift = 0.0;       // horizontal offset, in CSS pixels at a 100 px tall card
	double clip_top = 0.0;    // visible band, as fractions of the height;
	double clip_bottom = 1.0; // the full height when not clipped

	bool clipped() const { return clip_top > 0.0 || clip_bottom < 1.0; }
};

GlitchFrame glitch_frame(double seconds);

} // namespace goalbar

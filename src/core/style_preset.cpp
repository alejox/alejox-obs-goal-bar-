#include "core/style_preset.hpp"

#include <cmath>

namespace goalbar {

std::string_view style_preset_id(StylePreset preset)
{
	switch (preset) {
	case StylePreset::Minimal:
		return "minimal";
	case StylePreset::Bold:
		return "bold";
	case StylePreset::Neon:
		return "neon";
	case StylePreset::Glass:
		return "glass";
	case StylePreset::Pill:
		return "pill";
	case StylePreset::MacOS:
		return "macos";
	case StylePreset::Line:
		return "line";
	case StylePreset::Synthwave:
		return "synthwave";
	case StylePreset::NeonCity:
		return "neoncity";
	case StylePreset::Cyber:
		return "cyber";
	case StylePreset::Retro:
		return "retro";
	case StylePreset::Glitch:
		return "glitch";
	}
	return "minimal";
}

StylePreset style_preset_from_id(std::string_view id)
{
	for (StylePreset preset : kStylePresets) {
		if (style_preset_id(preset) == id)
			return preset;
	}
	return StylePreset::Minimal;
}

bool style_has_card(StylePreset preset)
{
	return preset != StylePreset::Minimal && preset != StylePreset::Bold && preset != StylePreset::Neon;
}

bool style_uses_custom_colors(StylePreset preset)
{
	return !style_has_card(preset) || preset == StylePreset::Glass || preset == StylePreset::Pill ||
	       preset == StylePreset::MacOS;
}

bool style_animates(StylePreset preset)
{
	return preset == StylePreset::Glitch;
}

GlitchFrame glitch_frame(double seconds)
{
	double phase = std::fmod(seconds, kGlitchPeriodSeconds) / kGlitchPeriodSeconds;
	if (phase < 0.0)
		phase += 1.0;

	// steps(1) keyframes: 92 % shift -3 px, band 10–45 %; 94 % shift 3 px,
	// band 60–92 %; 96 % shift -1 px, the band held until the loop restarts.
	GlitchFrame frame;
	if (phase < 0.92)
		return frame;
	if (phase < 0.94) {
		frame.step = 1;
		frame.shift = -3.0;
		frame.clip_top = 0.10;
		frame.clip_bottom = 0.45;
		return frame;
	}
	frame.step = phase < 0.96 ? 2 : 3;
	frame.shift = phase < 0.96 ? 3.0 : -1.0;
	frame.clip_top = 0.60;
	frame.clip_bottom = 0.92;
	return frame;
}

} // namespace goalbar

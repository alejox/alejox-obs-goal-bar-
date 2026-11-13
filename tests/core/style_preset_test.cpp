#include "core/style_preset.hpp"

#include "test_harness.hpp"

#include <string_view>

using namespace goalbar;

TEST_CASE(style_ids_are_stable)
{
	CHECK(style_preset_id(StylePreset::Minimal) == std::string_view("minimal"));
	CHECK(style_preset_id(StylePreset::Bold) == std::string_view("bold"));
	CHECK(style_preset_id(StylePreset::Neon) == std::string_view("neon"));
}

// Same ids as the shared styles in multi-chat and music-now-playing.
TEST_CASE(shared_style_ids_match_the_other_plugins)
{
	CHECK(style_preset_id(StylePreset::Glass) == std::string_view("glass"));
	CHECK(style_preset_id(StylePreset::Pill) == std::string_view("pill"));
	CHECK(style_preset_id(StylePreset::MacOS) == std::string_view("macos"));
	CHECK(style_preset_id(StylePreset::Line) == std::string_view("line"));
	CHECK(style_preset_id(StylePreset::Synthwave) == std::string_view("synthwave"));
	CHECK(style_preset_id(StylePreset::NeonCity) == std::string_view("neoncity"));
	CHECK(style_preset_id(StylePreset::Cyber) == std::string_view("cyber"));
	CHECK(style_preset_id(StylePreset::Retro) == std::string_view("retro"));
	CHECK(style_preset_id(StylePreset::Glitch) == std::string_view("glitch"));
}

TEST_CASE(style_ids_round_trip)
{
	for (StylePreset preset : kStylePresets)
		CHECK(style_preset_from_id(style_preset_id(preset)) == preset);
}

TEST_CASE(unknown_style_id_falls_back_to_minimal)
{
	CHECK(style_preset_from_id("glitter") == StylePreset::Minimal);
	CHECK(style_preset_from_id("") == StylePreset::Minimal);
	CHECK(style_preset_from_id("solid") == StylePreset::Minimal);
}

TEST_CASE(original_presets_have_no_card)
{
	CHECK(!style_has_card(StylePreset::Minimal));
	CHECK(!style_has_card(StylePreset::Bold));
	CHECK(!style_has_card(StylePreset::Neon));
	CHECK(style_has_card(StylePreset::Glass));
	CHECK(style_has_card(StylePreset::Glitch));
}

// Like the other plugins: the glass styles take the user's colors, the
// themed ones bring their own.
TEST_CASE(only_untinted_styles_use_the_user_colors)
{
	CHECK(style_uses_custom_colors(StylePreset::Minimal));
	CHECK(style_uses_custom_colors(StylePreset::Bold));
	CHECK(style_uses_custom_colors(StylePreset::Neon));
	CHECK(style_uses_custom_colors(StylePreset::Glass));
	CHECK(style_uses_custom_colors(StylePreset::Pill));
	CHECK(style_uses_custom_colors(StylePreset::MacOS));
	CHECK(!style_uses_custom_colors(StylePreset::Line));
	CHECK(!style_uses_custom_colors(StylePreset::Synthwave));
	CHECK(!style_uses_custom_colors(StylePreset::NeonCity));
	CHECK(!style_uses_custom_colors(StylePreset::Cyber));
	CHECK(!style_uses_custom_colors(StylePreset::Retro));
	CHECK(!style_uses_custom_colors(StylePreset::Glitch));
}

TEST_CASE(only_glitch_animates)
{
	for (StylePreset preset : kStylePresets)
		CHECK(style_animates(preset) == (preset == StylePreset::Glitch));
}

// The CSS keyframes: still until 91 %, then three short jumps in a 4 s loop.
TEST_CASE(glitch_is_still_most_of_the_loop)
{
	for (double t : {0.0, 1.0, 2.5, 3.6, 3.639}) {
		const GlitchFrame frame = glitch_frame(t);
		CHECK(frame.step == 0);
		CHECK(frame.shift == 0.0);
		CHECK(!frame.clipped());
	}
}

TEST_CASE(glitch_jumps_left_then_right_then_settles)
{
	const GlitchFrame first = glitch_frame(3.70); // 92.5 %
	CHECK(first.step == 1);
	CHECK(first.shift == -3.0);
	CHECK(first.clip_top == 0.10);
	CHECK(first.clip_bottom == 0.45);

	const GlitchFrame second = glitch_frame(3.78); // 94.5 %
	CHECK(second.step == 2);
	CHECK(second.shift == 3.0);
	CHECK(second.clip_top == 0.60);
	CHECK(second.clip_bottom == 0.92);

	const GlitchFrame third = glitch_frame(3.90); // 97.5 %
	CHECK(third.step == 3);
	CHECK(third.shift == -1.0);
	CHECK(third.clipped());
}

TEST_CASE(glitch_loops_every_period)
{
	CHECK(glitch_frame(3.70 + kGlitchPeriodSeconds).step == 1);
	CHECK(glitch_frame(kGlitchPeriodSeconds).step == 0);
	CHECK(glitch_frame(-0.1).step == 3); // negative clocks wrap too
}

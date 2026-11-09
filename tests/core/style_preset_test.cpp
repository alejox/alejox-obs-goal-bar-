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

TEST_CASE(style_ids_round_trip)
{
	for (StylePreset preset : kStylePresets)
		CHECK(style_preset_from_id(style_preset_id(preset)) == preset);
}

TEST_CASE(unknown_style_id_falls_back_to_minimal)
{
	CHECK(style_preset_from_id("glitter") == StylePreset::Minimal);
	CHECK(style_preset_from_id("") == StylePreset::Minimal);
}

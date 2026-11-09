#include "core/style_preset.hpp"

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

} // namespace goalbar

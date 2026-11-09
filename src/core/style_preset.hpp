// Pure core: no OBS, no Qt.
#pragma once

#include <string_view>

namespace goalbar {

enum class StylePreset { Minimal, Bold, Neon };

inline constexpr StylePreset kStylePresets[] = {StylePreset::Minimal, StylePreset::Bold, StylePreset::Neon};

// Stable ids used for settings persistence. Never change an existing id.
std::string_view style_preset_id(StylePreset preset);

// Unknown ids fall back to Minimal, so a stale setting still renders.
StylePreset style_preset_from_id(std::string_view id);

} // namespace goalbar

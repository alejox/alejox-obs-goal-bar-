// Render adapter: Qt only, no OBS.
#pragma once

#include "core/goal_view.hpp"
#include "core/style_preset.hpp"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>

namespace goalbar::render {

struct BarStyle {
	StylePreset preset = StylePreset::Minimal;
	QColor accent{255, 61, 110};
	QColor text{255, 255, 255};
};

// Paints one Goal Bar frame into a transparent ARGB32_Premultiplied image.
// Safe to call from any thread while a QGuiApplication exists.
class GoalBarPainter {
public:
	// `reached_text` is the localized "Goal reached!" label, shown when the
	// view is reached.
	static QImage paint(const GoalView &view, const BarStyle &style, QSize size, const QString &reached_text);
};

} // namespace goalbar::render

// Render adapter internals shared by the bar-only and the card painters. Qt only.
#pragma once

#include "core/celebration.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QSizeF>
#include <QStringList>

#include <algorithm>

namespace goalbar::render {

inline QColor with_alpha(QColor color, int alpha)
{
	color.setAlpha(std::clamp(alpha, 0, 255));
	return color;
}

inline QFont font_with(qreal pixel_size, QFont::Weight weight, const QStringList &families = {})
{
	QFont font;
	if (!families.isEmpty())
		font.setFamilies(families);
	font.setPixelSize(std::max(1, static_cast<int>(pixel_size)));
	font.setWeight(weight);
	font.setStyleStrategy(QFont::PreferAntialias);
	return font;
}

// A track whose ends are fully rounded (or square for `square`).
inline QPainterPath bar_path(const QRectF &rect, bool square = false)
{
	QPainterPath path;
	if (square) {
		path.addRect(rect);
		return path;
	}
	const qreal radius = rect.height() / 2;
	path.addRoundedRect(rect, radius, radius);
	return path;
}

// Celebration: a pulsing halo around `track` plus a brightening of the bar itself.
void draw_celebration_glow(QPainter &painter, const QPainterPath &track, qreal bar_height, const QColor &accent,
			   double glow);

void draw_confetti(QPainter &painter, QSizeF size, const CelebrationFrame &frame, const QColor &accent);

} // namespace goalbar::render

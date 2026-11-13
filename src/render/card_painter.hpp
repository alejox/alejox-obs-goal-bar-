// Render adapter: Qt only, no OBS.
#pragma once

#include "core/celebration.hpp"
#include "core/goal_view.hpp"
#include "render/goal_bar_painter.hpp"

#include <QPainter>
#include <QSizeF>
#include <QString>

namespace goalbar::render {

// Paints a card style (Glass, Pill, macOS, Line, Synthwave, Neon city,
// Cyberpunk, Retro, Glitch): the same looks as the backgrounds of multi-chat
// and the cards of music-now-playing, drawn with QPainter.
void paint_card(QPainter &painter, const GoalView &view, const BarStyle &style, QSizeF size,
		const QString &reached_text, const CelebrationFrame *celebration, double clock_seconds);

} // namespace goalbar::render

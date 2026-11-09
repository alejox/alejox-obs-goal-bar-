#include "render/goal_bar_painter.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace goalbar::render {
namespace {

// Where each part of the frame goes, in pixels.
struct Layout {
	QRectF text_row;  // title on the left, value on the right
	QRectF bar;       // the progress track
	qreal text_px;    // pixel size of the text row font
	bool value_in_bar; // Bold draws the value inside the bar
};

Layout layout_for(StylePreset preset, QSizeF size)
{
	const qreal w = size.width();
	const qreal h = size.height();
	// Neon keeps room around the bar for its glow.
	const qreal pad = preset == StylePreset::Neon ? h * 0.12 : h * 0.06;
	const qreal bar_share = preset == StylePreset::Bold ? 0.50 : preset == StylePreset::Neon ? 0.30 : 0.20;
	const qreal bar_h = std::max<qreal>(4.0, (h - 2 * pad) * bar_share);
	const qreal gap = h * 0.06;

	Layout layout;
	layout.bar = QRectF(pad, h - pad - bar_h, w - 2 * pad, bar_h);
	layout.text_row = QRectF(pad, pad * 0.5, w - 2 * pad, std::max<qreal>(1.0, layout.bar.top() - gap - pad * 0.5));
	// Minimal stays understated: its text is capped well below the row height.
	const qreal text_cap = preset == StylePreset::Minimal ? h * 0.30 : h;
	layout.text_px = std::max<qreal>(8.0, std::min(layout.text_row.height() * 0.78, text_cap));
	layout.value_in_bar = preset == StylePreset::Bold;
	return layout;
}

QFont font_with(qreal pixel_size, QFont::Weight weight)
{
	QFont font;
	font.setPixelSize(std::max(1, static_cast<int>(pixel_size)));
	font.setWeight(weight);
	font.setStyleStrategy(QFont::PreferAntialias);
	return font;
}

QColor with_alpha(QColor color, int alpha)
{
	color.setAlpha(alpha);
	return color;
}

QPainterPath rounded(const QRectF &rect)
{
	QPainterPath path;
	const qreal radius = rect.height() / 2;
	path.addRoundedRect(rect, radius, radius);
	return path;
}

// Draws text elided to fit `rect`. Neon adds a soft accent halo behind it.
void draw_text(QPainter &painter, const QRectF &rect, const QString &text, Qt::Alignment align, const QColor &color,
	       const QColor *halo)
{
	const QFontMetricsF metrics(painter.font());
	const QString shown = metrics.elidedText(text, Qt::ElideRight, rect.width());
	if (halo) {
		const qreal spread = std::max<qreal>(1.0, painter.font().pixelSize() * 0.06);
		painter.setPen(with_alpha(*halo, 90));
		for (const QPointF offset : {QPointF(spread, 0), QPointF(-spread, 0), QPointF(0, spread), QPointF(0, -spread)})
			painter.drawText(rect.translated(offset), align, shown);
	}
	painter.setPen(color);
	painter.drawText(rect, align, shown);
}

void draw_track(QPainter &painter, StylePreset preset, const QRectF &bar, const BarStyle &style)
{
	const QPainterPath track = rounded(bar);
	switch (preset) {
	case StylePreset::Minimal:
		painter.fillPath(track, with_alpha(style.text, 60));
		break;
	case StylePreset::Bold:
		painter.fillPath(track, QColor(0, 0, 0, 120));
		break;
	case StylePreset::Neon: {
		// Glow: wide faint strokes under a sharp outline.
		const qreal glow = bar.height() * 0.35;
		painter.setBrush(Qt::NoBrush);
		for (int i = 4; i >= 1; --i) {
			painter.setPen(QPen(with_alpha(style.accent, 22 * (5 - i)), glow * i / 2.0));
			painter.drawPath(track);
		}
		painter.fillPath(track, QColor(10, 10, 20, 200));
		break;
	}
	}
}

void draw_fill(QPainter &painter, StylePreset preset, const QRectF &bar, double progress, const BarStyle &style)
{
	const qreal fill_width = bar.width() * std::clamp(progress, 0.0, 1.0);
	if (fill_width <= 0.0)
		return;

	// Clipping to the track keeps the left end rounded at any progress, and
	// starting the fill one bar-height to the left keeps the right end rounded.
	painter.save();
	painter.setClipPath(rounded(bar));
	const QRectF fill(bar.left() - bar.height(), bar.top(), fill_width + bar.height(), bar.height());
	if (preset == StylePreset::Bold) {
		QLinearGradient gradient(fill.topLeft(), fill.bottomLeft());
		gradient.setColorAt(0.0, style.accent.lighter(130));
		gradient.setColorAt(1.0, style.accent);
		painter.fillPath(rounded(fill), gradient);
	} else {
		painter.fillPath(rounded(fill), style.accent);
	}
	painter.restore();
}

void draw_neon_outline(QPainter &painter, const QRectF &bar, const BarStyle &style)
{
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(style.accent.lighter(140), std::max<qreal>(1.5, bar.height() * 0.06)));
	painter.drawPath(rounded(bar));
}

} // namespace

QImage GoalBarPainter::paint(const GoalView &view, const BarStyle &style, QSize size, const QString &reached_text)
{
	QImage image(size, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	if (size.isEmpty())
		return image;

	const StylePreset preset = style.preset;
	const Layout layout = layout_for(preset, QSizeF(size));

	QPainter painter(&image);
	painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

	draw_track(painter, preset, layout.bar, style);
	draw_fill(painter, preset, layout.bar, view.progress, style);
	if (preset == StylePreset::Neon)
		draw_neon_outline(painter, layout.bar, style);

	const QColor *halo = preset == StylePreset::Neon ? &style.accent : nullptr;
	const QString title = view.reached ? reached_text : QString::fromStdString(view.title);
	const QString value = QString::fromStdString(view.value_label);
	const QFont::Weight weight = preset == StylePreset::Minimal ? QFont::DemiBold : QFont::Bold;

	painter.setFont(font_with(layout.text_px, weight));
	QRectF title_rect = layout.text_row;
	if (layout.value_in_bar) {
		const qreal inset = layout.bar.height() * 0.4;
		painter.save();
		painter.setFont(font_with(layout.bar.height() * 0.5, QFont::Bold));
		draw_text(painter, layout.bar.adjusted(inset, 0, -inset, 0), value, Qt::AlignRight | Qt::AlignVCenter,
			  style.text, nullptr);
		painter.restore();
	} else {
		const QFontMetricsF metrics(painter.font());
		const qreal value_width = std::min(metrics.horizontalAdvance(value) + 1.0, layout.text_row.width());
		draw_text(painter, layout.text_row, value, Qt::AlignRight | Qt::AlignBottom, style.text, halo);
		title_rect.setRight(layout.text_row.right() - value_width - layout.text_px * 0.6);
	}

	const QColor title_color = view.reached ? style.accent.lighter(120) : style.text;
	if (title_rect.width() > 0)
		draw_text(painter, title_rect, title, Qt::AlignLeft | Qt::AlignBottom, title_color, halo);

	return image;
}

} // namespace goalbar::render

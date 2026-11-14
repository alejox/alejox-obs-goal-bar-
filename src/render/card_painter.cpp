#include "render/card_painter.hpp"

#include "core/style_preset.hpp"
#include "render/paint_common.hpp"

#include <QFontMetricsF>
#include <QImage>
#include <QLinearGradient>
#include <QPen>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

// The values below come from the shared styles: multi-chat
// data/overlay/styles.css and music-now-playing obs-plugin/data/overlay/index.html.
// Lengths are CSS pixels for a 100 px tall card, scaled by `u`.

namespace goalbar::render {
namespace {

constexpr qreal kPi = 3.14159265358979323846;

QColor rgba(int r, int g, int b, double a)
{
	return QColor(r, g, b, static_cast<int>(std::lround(std::clamp(a, 0.0, 1.0) * 255)));
}

QColor mix(const QColor &a, const QColor &b, double t)
{
	return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
				static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
				static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t));
}

enum class Shape { Rounded, Pill, Cut };

struct TextFx {
	QColor glow;          // invalid: no glow
	qreal glow_px = 0;    // reach of the glow
	qreal chroma_px = 0;  // Glitch: red/cyan split
	qreal chroma_alpha = 0.85;
	bool shadow = false;  // glass: dark shadow under the text
};

struct Theme {
	Shape shape = Shape::Rounded;
	qreal radius = 16;
	qreal shadow = 0.32; // drop shadow opacity, 0 for none
	QColor title{255, 255, 255};
	QColor value{255, 255, 255};
	QColor reached{255, 255, 255};
	QColor accent{255, 255, 255}; // bar fill and confetti
	QColor track = rgba(255, 255, 255, 0.18);
	QColor track_border;
	qreal track_border_w = 0;
	bool square_bar = false;
	bool fill_glow = false;
	TextFx title_fx;
	TextFx value_fx;
	QStringList title_families;
	QStringList value_families;
	QFont::Weight title_weight = QFont::Bold;
	QFont::Weight value_weight = QFont::Bold;
	qreal title_scale = 1.0;
	qreal value_scale = 1.0;
	qreal pad_left = 24;
	qreal pad_right = 24;
	qreal title_inset = 0; // room for the macOS window buttons
	qreal bar_share = 0.24; // bar height, as a share of the content height
	QColor fill_end;        // valid: the fill is a left-to-right gradient accent → fill_end
	QColor outer_glow;      // valid: a neon glow around the card
	qreal outer_glow_px = 0;
	bool uppercase_title = false;
	qreal value_spacing = 100; // QFont percentage letter spacing of the value
	bool equalizer = false;    // GTA 6: three bars in front of the value
};

const QStringList kScriptFonts{"Mr Dafoe", "Brush Script MT", "Segoe Script", "Snell Roundhand"};
const QStringList kHudFonts{"Orbitron", "Eurostile", "Bank Gothic", "Arial Black"};
const QStringList kVhsFonts{"VT323", "Courier New", "Courier", "Monospace"};
const QStringList kViceFonts{"Anton", "Bebas Neue", "Impact", "Arial Narrow"};

// The Vice City pink → orange of the GTA 6 styles.
const QColor kVicePink(0xff, 0x3d, 0x9a);
const QColor kViceOrange(0xff, 0x8a, 0x3d);

// Size of the GTA 6 vinyl against the card height, and its distance from the left edge.
constexpr qreal kVinylShare = 0.8;
constexpr qreal kVinylLeft = 16;

Theme glass_theme(const BarStyle &style)
{
	Theme theme;
	theme.shadow = 0.3;
	theme.title = style.text;
	theme.value = style.text;
	theme.reached = style.accent.lighter(120);
	theme.accent = style.accent;
	theme.track = rgba(255, 255, 255, 0.22);
	theme.title_fx.shadow = true;
	theme.value_fx.shadow = true;
	return theme;
}

Theme theme_for(const BarStyle &style)
{
	Theme theme;
	switch (style.preset) {
	case StylePreset::Pill:
		theme = glass_theme(style);
		theme.shape = Shape::Pill;
		theme.pad_left = theme.pad_right = 42;
		return theme;
	case StylePreset::MacOS:
		theme = glass_theme(style);
		theme.radius = 14;
		theme.title_inset = 62;
		return theme;
	case StylePreset::Line:
		theme.radius = 18;
		theme.reached = QColor(255, 255, 255);
		theme.value = rgba(255, 255, 255, 0.85);
		theme.pad_left = 38;
		theme.pad_right = 38;
		return theme;
	case StylePreset::Synthwave:
		theme.shadow = 0.4;
		theme.title = QColor(0xff, 0x3f, 0xdc);
		theme.value = QColor(0x35, 0xe0, 0xff);
		theme.reached = QColor(0xff, 0xd2, 0x3f);
		theme.accent = QColor(0xff, 0x2b, 0xd6);
		theme.track = rgba(255, 43, 214, 0.18);
		theme.track_border = rgba(255, 43, 214, 0.7);
		theme.track_border_w = 1;
		theme.fill_glow = true;
		theme.title_fx = {rgba(255, 43, 214, 0.85), 10};
		theme.value_fx = {rgba(53, 224, 255, 0.6), 6};
		theme.title_families = kScriptFonts;
		theme.title_weight = QFont::Normal;
		theme.title_scale = 1.3;
		return theme;
	case StylePreset::NeonCity:
		theme.shadow = 0.4;
		theme.title = QColor(0xff, 0x4f, 0xe0);
		theme.value = QColor(0x2f, 0xe0, 0xff);
		theme.reached = QColor(0x2f, 0xe0, 0xff);
		theme.accent = QColor(0xff, 0x3f, 0xd8);
		theme.track = rgba(47, 224, 255, 0.25);
		theme.fill_glow = true;
		theme.title_fx = {rgba(255, 63, 216, 0.6), 8};
		theme.value_fx = {rgba(47, 224, 255, 0.45), 6};
		return theme;
	case StylePreset::Cyber:
		theme.shape = Shape::Cut;
		theme.shadow = 0;
		theme.title = QColor(0xea, 0xf6, 0xff);
		theme.value = QColor(0xff, 0x3f, 0xd8);
		theme.reached = QColor(0x22, 0xe4, 0xff);
		theme.accent = QColor(0x22, 0xe4, 0xff);
		theme.track = rgba(255, 43, 214, 0.25);
		theme.square_bar = true;
		theme.fill_glow = true;
		theme.title_fx = {rgba(34, 228, 255, 0.7), 8};
		theme.value_fx = {rgba(255, 63, 216, 0.6), 6};
		theme.title_families = kHudFonts;
		theme.value_families = kHudFonts;
		theme.title_scale = 0.8;
		theme.value_scale = 0.8;
		theme.pad_left = 28;
		theme.pad_right = 28;
		return theme;
	case StylePreset::Retro:
		theme.radius = 12;
		theme.shadow = 0.4;
		theme.title = QColor(0x1c, 0x1a, 0x17);
		theme.value = QColor(0x1c, 0x1a, 0x17);
		theme.reached = QColor(0xb0, 0x41, 0x2c);
		theme.accent = QColor(0x1a, 0xa3, 0xa3);
		theme.track = QColor(0x1c, 0x1a, 0x17);
		theme.track_border = QColor(0x1c, 0x1a, 0x17);
		theme.track_border_w = 1.5;
		theme.title_weight = QFont::ExtraBold;
		theme.pad_right = 24 + 44; // the rainbow stripes
		return theme;
	case StylePreset::Glitch:
		theme.radius = 12;
		theme.title = QColor(0xf2, 0xf2, 0xf2);
		theme.value = QColor(0xf2, 0xf2, 0xf2);
		theme.reached = QColor(0x50, 0xe6, 0xf0);
		theme.accent = QColor(0xf2, 0xf2, 0xf2);
		theme.track = rgba(80, 220, 230, 0.25);
		theme.track_border = rgba(80, 220, 230, 0.7);
		theme.track_border_w = 1;
		theme.title_fx.chroma_px = 2;
		theme.value_fx.chroma_px = 1.5;
		theme.value_fx.chroma_alpha = 0.7;
		theme.title_families = kVhsFonts;
		theme.value_families = kVhsFonts;
		theme.title_weight = QFont::Normal;
		theme.value_weight = QFont::Normal;
		theme.title_scale = 1.25;
		theme.value_scale = 1.25;
		return theme;
	case StylePreset::Gta6:
	case StylePreset::Gta6Disc:
		// Dark card with a pink neon rim, condensed uppercase title and a pink → orange bar.
		theme.radius = 18;
		theme.shadow = 0.4;
		theme.outer_glow = rgba(255, 60, 160, 0.45);
		theme.outer_glow_px = 14;
		theme.title = QColor(255, 255, 255);
		theme.value = QColor(0xff, 0x4f, 0xa3);
		theme.reached = kViceOrange;
		theme.accent = QColor(0xff, 0x4f, 0xa3);
		theme.fill_end = kViceOrange;
		theme.track = rgba(255, 255, 255, 0.13);
		theme.bar_share = 0.16;
		theme.title_families = kViceFonts;
		theme.value_families = kViceFonts;
		theme.title_weight = QFont::Normal;
		theme.value_weight = QFont::Normal;
		theme.title_scale = 0.9; // Anton is tall: keep its caps inside the padding
		theme.value_scale = 0.72;
		theme.value_spacing = 108;
		theme.uppercase_title = true;
		theme.equalizer = true;
		theme.pad_left = 26;
		theme.pad_right = 26;
		if (style.preset == StylePreset::Gta6Disc)
			theme.pad_left = kVinylLeft + kVinylShare * 100 + 18;
		return theme;
	case StylePreset::Glass:
	default:
		return glass_theme(style);
	}
}

struct CardLayout {
	QRectF card;
	qreal margin; // room around the card for the drop shadow
	qreal u;      // CSS pixel at a 100 px tall card
};

CardLayout card_layout(QSizeF size)
{
	const qreal margin = std::clamp(size.height() * 0.1, 2.0, 40.0);
	const QRectF card(margin, margin * 0.7, std::max<qreal>(1.0, size.width() - 2 * margin),
			  std::max<qreal>(1.0, size.height() - margin * 2.0));
	return {card, margin, std::max<qreal>(0.2, card.height() / 100.0)};
}

QPainterPath cut_corners(const QRectF &r, qreal cut)
{
	QPainterPath path;
	path.moveTo(r.left() + cut, r.top());
	path.lineTo(r.right(), r.top());
	path.lineTo(r.right(), r.bottom() - cut);
	path.lineTo(r.right() - cut, r.bottom());
	path.lineTo(r.left(), r.bottom());
	path.lineTo(r.left(), r.top() + cut);
	path.closeSubpath();
	return path;
}

qreal cyber_cut(const QRectF &card, qreal u)
{
	return std::min(16 * u, card.height() / 3);
}

QPainterPath shape_path(const Theme &theme, const QRectF &card, qreal u)
{
	QPainterPath path;
	switch (theme.shape) {
	case Shape::Cut:
		return cut_corners(card, cyber_cut(card, u));
	case Shape::Pill:
		path.addRoundedRect(card, card.height() / 2, card.height() / 2);
		return path;
	case Shape::Rounded:
	default: {
		const qreal radius = std::min(theme.radius * u, card.height() / 2);
		path.addRoundedRect(card, radius, radius);
		return path;
	}
	}
}

// CSS linear-gradient(<deg>, …) over `r`: 0deg points up, 90deg right.
QLinearGradient css_gradient(const QRectF &r, qreal degrees)
{
	const qreal rad = degrees * kPi / 180.0;
	const QPointF dir(std::sin(rad), -std::cos(rad));
	const qreal length = std::abs(r.width() * dir.x()) + std::abs(r.height() * dir.y());
	return QLinearGradient(r.center() - dir * (length / 2), r.center() + dir * (length / 2));
}

// Soft dark shadow below the card, kept inside the source so OBS never cuts it.
void draw_drop_shadow(QPainter &p, const QPainterPath &shape, QSizeF size, qreal margin, qreal u, qreal alpha)
{
	if (alpha <= 0.0)
		return;
	const qreal dy = std::min(6 * u, margin * 0.35);
	const qreal spread = std::min(18 * u, margin * 0.9);
	QPainterPath outside;
	outside.addRect(QRectF(QPointF(0, 0), size));
	outside = outside.subtracted(shape);

	p.save();
	p.setClipPath(outside);
	const QPainterPath moved = shape.translated(0, dy);
	constexpr int kLayers = 6;
	p.setBrush(Qt::NoBrush);
	for (int i = kLayers; i >= 1; --i) {
		p.setPen(QPen(rgba(0, 0, 0, alpha * 0.13), 2 * spread * i / kLayers, Qt::SolidLine, Qt::RoundCap,
			      Qt::RoundJoin));
		p.drawPath(moved);
	}
	p.fillPath(moved, rgba(0, 0, 0, alpha * 0.8));
	p.restore();
}

// CSS inset box-shadow: a glow along the inside edge.
void draw_inset_glow(QPainter &p, const QPainterPath &shape, const QColor &color, qreal reach)
{
	p.save();
	p.setClipPath(shape, Qt::IntersectClip);
	p.setBrush(Qt::NoBrush);
	for (int i = 4; i >= 1; --i) {
		p.setPen(QPen(with_alpha(color, color.alpha() * 3 / 10), reach * i / 2.0));
		p.drawPath(shape);
	}
	p.restore();
}

void draw_border(QPainter &p, const QPainterPath &shape, const QColor &color, qreal width)
{
	p.save();
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(color, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
	p.drawPath(shape);
	p.restore();
}

// repeating-linear-gradient(<deg>, color 0 <on>, transparent <on> <period>) inside `rect`.
void draw_hatch(QPainter &p, const QRectF &rect, qreal degrees, qreal on, qreal period, const QColor &color)
{
	p.save();
	p.setClipRect(rect, Qt::IntersectClip);
	p.translate(rect.center());
	p.rotate(degrees);
	const qreal reach = std::hypot(rect.width(), rect.height()) / 2 + period;
	for (qreal y = -reach; y < reach; y += period)
		p.fillRect(QRectF(-reach, y, 2 * reach, on), color);
	p.restore();
}

// Striped sunset sun: solid on top, horizontal slits below `solid`.
void draw_sun(QPainter &p, const QRectF &box, qreal solid, qreal stripe_on, qreal stripe_period,
	      const QLinearGradient &fill, const QColor &glow, qreal glow_px)
{
	QPainterPath mask;
	mask.addRect(QRectF(box.left(), box.top(), box.width(), box.height() * solid));
	for (qreal t = solid - std::fmod(solid, stripe_period); t < 1.0; t += stripe_period) {
		QPainterPath stripe;
		stripe.addRect(QRectF(box.left(), box.top() + t * box.height(), box.width(), stripe_on * box.height()));
		mask = mask.united(stripe);
	}
	QPainterPath circle;
	circle.addEllipse(box);
	const QPainterPath sun = circle.intersected(mask);

	p.save();
	p.setBrush(Qt::NoBrush);
	for (int i = 3; i >= 1; --i) {
		p.setPen(QPen(with_alpha(glow, glow.alpha() / 4), glow_px * i / 1.5, Qt::SolidLine, Qt::RoundCap,
			      Qt::RoundJoin));
		p.drawPath(sun);
	}
	p.fillPath(sun, fill);
	p.restore();
}

// The skyline of the shared styles (an SVG path in a 240 × 60 tile), as rectangles.
struct Building {
	int x, top, w, h;
};
constexpr Building kSkyline[] = {
	{0, 33, 12, 27},   {12, 8, 8, 52},    {15, 0, 2, 8},     {20, 29, 16, 31},  {27, 22, 2, 7},
	{38, 27, 8, 33},   {41, 20, 2, 7},    {46, 35, 16, 25},  {62, 5, 16, 55},   {78, 7, 8, 53},
	{87, 33, 14, 27},  {102, 31, 16, 29}, {109, 23, 2, 8},   {118, 36, 12, 24}, {130, 39, 16, 21},
	{148, 8, 18, 52},  {167, 5, 14, 55},  {182, 27, 12, 33}, {194, 6, 8, 54},   {204, 14, 12, 46},
	{209, 10, 2, 4},   {216, 16, 16, 44}, {223, 10, 2, 6},   {232, 16, 14, 44}, {238, 7, 2, 9}};

// Tiles anchored to the right edge of `area`, repeated to the left.
QPainterPath skyline(const QRectF &area)
{
	QPainterPath path;
	path.setFillRule(Qt::WindingFill);
	const qreal scale = area.height() / 60.0;
	const qreal tile = 240.0 * scale;
	for (qreal x0 = area.right() - tile; x0 + tile > area.left(); x0 -= tile) {
		for (const Building &b : kSkyline)
			path.addRect(QRectF(x0 + b.x * scale, area.top() + b.top * scale, b.w * scale, b.h * scale));
	}
	QPainterPath clip;
	clip.addRect(area);
	return path.intersected(clip);
}

// Synthwave floor: magenta lines in perspective, fading toward the horizon.
void draw_synth_grid(QPainter &p, const QRectF &card, qreal u)
{
	const qreal height = card.height() * 0.55;
	const qreal bottom = card.bottom();
	const qreal horizon = bottom - height;
	QLinearGradient fade(0, bottom, 0, horizon);
	fade.setColorAt(0.0, rgba(255, 43, 214, 0.75));
	fade.setColorAt(0.1, rgba(255, 43, 214, 0.75));
	fade.setColorAt(0.95, rgba(255, 43, 214, 0.0));
	p.save();
	p.setPen(QPen(QBrush(fade), std::max<qreal>(1.0, u)));
	const qreal cx = card.center().x();
	const qreal spacing = 26 * u;
	constexpr qreal kShrink = 0.4; // how much narrower the far edge is
	for (qreal dx = 0; dx < card.width() * 1.5; dx += spacing) {
		for (const qreal x : {cx + dx, cx - dx}) {
			p.drawLine(QPointF(x, bottom), QPointF(cx + (x - cx) * kShrink, horizon));
			if (dx == 0)
				break;
		}
	}
	for (int j = 0; j < 40; ++j) {
		const qreal y = bottom - height * (1.0 - std::pow(0.74, j));
		if (y - horizon < u)
			break;
		p.drawLine(QPointF(card.left(), y), QPointF(card.right(), y));
	}
	p.restore();
}

void draw_glass(QPainter &p, const QPainterPath &shape, const QRectF &card, qreal u, qreal alpha, const QColor &tint)
{
	// Translucent and tinted, with a shine and a rim of light (OBS can't blur what is behind).
	p.fillPath(shape, with_alpha(mix(QColor(18, 18, 24), tint, 0.22), static_cast<int>(alpha * 0.6 * 255)));
	p.save();
	p.setClipPath(shape, Qt::IntersectClip);
	QLinearGradient shine = css_gradient(card, 135);
	shine.setColorAt(0.0, rgba(255, 255, 255, 0.22));
	shine.setColorAt(0.38, rgba(255, 255, 255, 0.07));
	shine.setColorAt(0.58, rgba(255, 255, 255, 0.0));
	p.fillRect(card, shine);
	QLinearGradient top = css_gradient(card, 180);
	top.setColorAt(0.0, rgba(255, 255, 255, 0.08));
	top.setColorAt(0.35, rgba(255, 255, 255, 0.0));
	p.fillRect(card, top);
	p.fillRect(QRectF(card.left(), card.top(), card.width(), u), rgba(255, 255, 255, 0.35));
	p.fillRect(QRectF(card.left(), card.bottom() - u, card.width(), u), rgba(0, 0, 0, 0.12));
	p.restore();
	draw_border(p, shape, rgba(255, 255, 255, 0.28), u);
}

void draw_line(QPainter &p, const QPainterPath &shape, const QRectF &card, qreal u, qreal alpha)
{
	p.fillPath(shape, rgba(0, 0, 0, alpha));
	const qreal top = card.top() + card.height() * 0.22;
	const qreal height = card.height() * 0.56;
	const QColor white(255, 255, 255);
	draw_hatch(p, QRectF(card.left() + 10 * u, top, 10 * u, height), -55, 3 * u, 9 * u, white);
	draw_hatch(p, QRectF(card.right() - 20 * u, top, 10 * u, height), -55, 3 * u, 9 * u, white);
	draw_border(p, shape, white, 2 * u);
}

void draw_synthwave(QPainter &p, const QPainterPath &shape, const QRectF &card, qreal u, qreal alpha)
{
	QLinearGradient sky = css_gradient(card, 180);
	sky.setColorAt(0.0, rgba(40, 6, 70, alpha));
	sky.setColorAt(1.0, rgba(90, 10, 90, alpha));
	p.fillPath(shape, sky);

	p.save();
	p.setClipPath(shape, Qt::IntersectClip);
	const qreal size = 118 * u;
	const QRectF sun(card.left() + card.width() * 0.58 - size / 2, card.top() - 26 * u, size, size);
	QLinearGradient fill(sun.topLeft(), sun.bottomLeft());
	fill.setColorAt(0.10, QColor(0xff, 0xd2, 0x3f));
	fill.setColorAt(0.45, QColor(0xff, 0x7b, 0x2e));
	fill.setColorAt(0.80, QColor(0xff, 0x2a, 0x8a));
	draw_sun(p, sun, 0.52, 0.07, 0.11, fill, rgba(255, 90, 140, 0.6), 10 * u);
	draw_synth_grid(p, card, u);
	p.restore();

	draw_inset_glow(p, shape, rgba(255, 43, 214, 0.55), 14 * u);
	draw_border(p, shape, QColor(0xff, 0x2b, 0xd6), 2 * u);
}

void draw_neon_city(QPainter &p, const QPainterPath &shape, const QRectF &card, qreal u, qreal alpha)
{
	QLinearGradient night = css_gradient(card, 115);
	night.setColorAt(0.0, rgba(14, 10, 52, alpha));
	night.setColorAt(1.0, rgba(36, 10, 72, alpha));
	p.fillPath(shape, night);

	p.save();
	p.setClipPath(shape, Qt::IntersectClip);
	const qreal size = 104 * u;
	// The cards of music-now-playing put it 40 px from the right; here the value
	// sits there, so the sun moves in between the title and the value.
	const QRectF sun(card.left() + card.width() * 0.64 - size / 2, card.top() - 6 * u, size, size);
	QLinearGradient fill(sun.topLeft(), sun.bottomLeft());
	fill.setColorAt(0.20, QColor(0xff, 0x3f, 0x8e));
	fill.setColorAt(0.75, QColor(0xff, 0x7a, 0x45));
	draw_sun(p, sun, 0.45, 0.08, 0.12, fill, rgba(255, 70, 140, 0.55), 12 * u);

	const qreal width = card.width() * 0.62;
	const QRectF city(card.right() - width, card.bottom() - card.height() * 0.86, width, card.height() * 0.68);
	QLinearGradient buildings(city.bottomLeft(), city.topLeft());
	buildings.setColorAt(0.0, QColor(0x12, 0x0a, 0x2e));
	buildings.setColorAt(1.0, QColor(0x2b, 0x14, 0x5a));
	p.fillPath(skyline(city), buildings);

	const QRectF water(card.right() - width, city.bottom(), width, card.bottom() - city.bottom());
	QLinearGradient haze(water.topLeft(), water.bottomLeft());
	haze.setColorAt(0.0, rgba(90, 30, 140, 0.6));
	haze.setColorAt(1.0, rgba(90, 30, 140, 0.0));
	p.fillRect(water, haze);
	for (qreal y = water.top(); y < water.bottom(); y += 4 * u)
		p.fillRect(QRectF(water.left(), y, water.width(), u), rgba(255, 63, 216, 0.45));
	p.restore();

	draw_inset_glow(p, shape, rgba(140, 70, 255, 0.4), 14 * u);
	draw_border(p, shape, rgba(160, 90, 255, 0.85), 1.5 * u);
}

void draw_cyber(QPainter &p, const QPainterPath &shape, const QRectF &card, qreal u, qreal alpha)
{
	// A cyan → magenta frame around a dark HUD panel.
	const qreal cut = cyber_cut(card, u);
	const QPainterPath inner = cut_corners(card.adjusted(2 * u, 2 * u, -2 * u, -2 * u), std::max<qreal>(0, cut - u));
	QLinearGradient frame = css_gradient(card, 115);
	frame.setColorAt(0.0, QColor(0x22, 0xd3, 0xff));
	frame.setColorAt(0.5, QColor(0x3a, 0x6b, 0xff));
	frame.setColorAt(1.0, QColor(0xff, 0x2b, 0xd6));
	p.fillPath(shape.subtracted(inner), frame);

	QLinearGradient panel = css_gradient(card, 120);
	panel.setColorAt(0.0, rgba(5, 12, 40, alpha));
	panel.setColorAt(1.0, rgba(20, 8, 50, alpha));
	p.fillPath(inner, panel);

	p.save();
	p.setClipPath(inner, Qt::IntersectClip);
	const qreal width = card.width() * 0.6;
	const QRectF city(card.right() - width, card.bottom() - card.height() * 0.8, width, card.height() * 0.8);
	QLinearGradient glow(city.bottomLeft(), city.topLeft());
	glow.setColorAt(0.0, QColor(0xff, 0x2b, 0xd6));
	glow.setColorAt(0.6, QColor(0x3a, 0x3b, 0xff));
	glow.setColorAt(1.0, QColor(0x1a, 0x1f, 0x6b));
	p.setOpacity(0.35);
	p.fillPath(skyline(city), glow);
	p.setOpacity(1.0);
	draw_hatch(p, QRectF(card.right() - 26 * u - 46 * u, card.top() + 6 * u, 46 * u, 5 * u), 115, 5 * u, 9 * u,
		   QColor(0x22, 0xd3, 0xff));
	p.restore();
}

void draw_retro(QPainter &p, const QPainterPath &shape, const QRectF &card, qreal u, qreal alpha)
{
	p.fillPath(shape, rgba(238, 228, 205, alpha));
	p.save();
	p.setClipPath(shape, Qt::IntersectClip);
	// Old paper: a few soft stains and a light spot.
	struct Spot {
		qreal x, y, reach;
		QColor color;
	};
	const Spot spots[] = {{0.18, 0.30, 0.30, rgba(140, 110, 60, 0.12)},
			      {0.70, 0.80, 0.35, rgba(140, 110, 60, 0.10)},
			      {0.85, 0.15, 0.30, rgba(255, 255, 255, 0.30)}};
	for (const Spot &spot : spots) {
		const QPointF center(card.left() + card.width() * spot.x, card.top() + card.height() * spot.y);
		const qreal far = std::hypot(std::max(spot.x, 1 - spot.x) * card.width(),
					     std::max(spot.y, 1 - spot.y) * card.height());
		QRadialGradient stain(center, far * spot.reach);
		stain.setColorAt(0.0, spot.color);
		stain.setColorAt(1.0, with_alpha(spot.color, 0));
		p.fillRect(card, stain);
	}
	// Rainbow stripes on the right.
	const QColor rainbow[] = {QColor(0xd8, 0x45, 0x2f), QColor(0xec, 0x8a, 0x2d), QColor(0xf2, 0xc1, 0x4b),
				  QColor(0x4a, 0xa3, 0x6c), QColor(0x2b, 0x6f, 0x9a)};
	const qreal band = 44 * u / 5;
	for (int i = 0; i < 5; ++i)
		p.fillRect(QRectF(card.right() - 44 * u + i * band, card.top(), band + 0.5, card.height()), rainbow[i]);
	p.restore();
	draw_inset_glow(p, shape, rgba(120, 90, 40, 0.35), 22 * u);
}

void draw_glitch(QPainter &p, const QPainterPath &shape, const QRectF &card, qreal u, qreal alpha)
{
	p.fillPath(shape, rgba(18, 18, 20, alpha));
	p.save();
	p.setClipPath(shape, Qt::IntersectClip);
	for (qreal y = card.top(); y < card.bottom(); y += 3 * u)
		p.fillRect(QRectF(card.left(), y, card.width(), std::max<qreal>(1.0, u)), rgba(255, 255, 255, 0.05));
	// Elliptical vignette: a radial gradient squashed to the card's proportions.
	p.translate(card.center());
	p.scale(1.0, card.height() / card.width());
	QRadialGradient vignette(QPointF(0, 0), card.width() / 2 * std::sqrt(2.0));
	vignette.setColorAt(0.5, rgba(0, 0, 0, 0));
	vignette.setColorAt(1.0, rgba(0, 0, 0, 0.45));
	p.fillRect(QRectF(-card.width(), -card.width(), 2 * card.width(), 2 * card.width()), vignette);
	p.restore();
	draw_border(p, shape, rgba(255, 255, 255, 0.14), u);
}

// GTA 6 vinyl: black grooves around a pink label, with a faint pink rim.
void draw_vinyl(QPainter &p, const QRectF &card, qreal u)
{
	const qreal d = card.height() * kVinylShare;
	const QRectF disc(card.left() + kVinylLeft * u, card.center().y() - d / 2, d, d);
	const QPointF c = disc.center();
	const qreal r = d / 2;
	p.save();
	p.setPen(Qt::NoPen);
	// Drop shadow under the record.
	for (int i = 3; i >= 1; --i) {
		p.setBrush(rgba(0, 0, 0, 0.12));
		p.drawEllipse(c + QPointF(0, 4 * u), r + 3 * u * i, r + 3 * u * i);
	}
	p.setBrush(QColor(0x15, 0x10, 0x15));
	p.drawEllipse(c, r, r);
	// repeating-radial-gradient(#151015 0 2px, #222 2px 3px): a lighter groove every 3 px.
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(0x2e, 0x2a, 0x2e), std::max<qreal>(0.6, u)));
	for (qreal k = 2.5 * u; k < r; k += 3 * u)
		p.drawEllipse(c, k, k);
	// A soft sheen so it reads as vinyl.
	QLinearGradient sheen(disc.topLeft(), disc.bottomRight());
	sheen.setColorAt(0.0, rgba(255, 255, 255, 0.0));
	sheen.setColorAt(0.45, rgba(255, 255, 255, 0.07));
	sheen.setColorAt(0.55, rgba(255, 255, 255, 0.0));
	p.setPen(Qt::NoPen);
	p.setBrush(sheen);
	p.drawEllipse(c, r, r);
	// Center label. Bigger than in music-now-playing, where the album art sits next to the
	// record; here the record is the only picture on the card.
	p.setBrush(QColor(0x2a, 0x0a, 0x22));
	p.drawEllipse(c, r * 0.36, r * 0.36);
	QLinearGradient label(c - QPointF(r * 0.33, 0), c + QPointF(r * 0.33, 0));
	label.setColorAt(0.0, kVicePink);
	label.setColorAt(1.0, kViceOrange);
	p.setBrush(label);
	p.drawEllipse(c, r * 0.32, r * 0.32);
	p.setBrush(QColor(0x2a, 0x0a, 0x22));
	p.drawEllipse(c, r * 0.03 + 0.5, r * 0.03 + 0.5);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(rgba(255, 92, 170, 0.35), std::max<qreal>(1.0, u)));
	p.drawEllipse(c, r, r);
	p.restore();
}

void draw_gta6(QPainter &p, const QPainterPath &shape, const QRectF &card, qreal u, qreal alpha, bool vinyl)
{
	QLinearGradient night = css_gradient(card, 120);
	night.setColorAt(0.0, rgba(48, 14, 40, alpha));
	night.setColorAt(1.0, rgba(26, 10, 26, alpha));
	p.fillPath(shape, night);
	draw_inset_glow(p, shape, rgba(255, 60, 160, 0.12), 18 * u);
	if (vinyl) {
		p.save();
		p.setClipPath(shape, Qt::IntersectClip);
		draw_vinyl(p, card, u);
		p.restore();
	}
	draw_border(p, shape, rgba(255, 92, 170, 0.75), 1.5 * u);
}

void draw_background(QPainter &p, const BarStyle &style, const QPainterPath &shape, const QRectF &card, qreal u)
{
	const qreal alpha = std::clamp(style.opacity, 0, 100) / 100.0;
	switch (style.preset) {
	case StylePreset::Line:
		draw_line(p, shape, card, u, alpha);
		break;
	case StylePreset::Synthwave:
		draw_synthwave(p, shape, card, u, alpha);
		break;
	case StylePreset::NeonCity:
		draw_neon_city(p, shape, card, u, alpha);
		break;
	case StylePreset::Cyber:
		draw_cyber(p, shape, card, u, alpha);
		break;
	case StylePreset::Retro:
		draw_retro(p, shape, card, u, alpha);
		break;
	case StylePreset::Glitch:
		draw_glitch(p, shape, card, u, alpha);
		break;
	case StylePreset::Gta6:
	case StylePreset::Gta6Disc:
		draw_gta6(p, shape, card, u, alpha, style.preset == StylePreset::Gta6Disc);
		break;
	default:
		draw_glass(p, shape, card, u, alpha, style.accent);
		break;
	}
}

// A neon glow outside the card (CSS box-shadow 0 0 <reach> <color>).
void draw_outer_glow(QPainter &p, const QPainterPath &shape, QSizeF size, qreal margin, const QColor &color,
		     qreal reach)
{
	if (!color.isValid() || reach <= 0.0)
		return;
	reach = std::min(reach, margin);
	QPainterPath outside;
	outside.addRect(QRectF(QPointF(0, 0), size));
	outside = outside.subtracted(shape);
	p.save();
	p.setClipPath(outside);
	p.setBrush(Qt::NoBrush);
	for (int i = 4; i >= 1; --i) {
		p.setPen(QPen(with_alpha(color, color.alpha() * 3 / 10), reach * i / 2.0, Qt::SolidLine, Qt::RoundCap,
			      Qt::RoundJoin));
		p.drawPath(shape);
	}
	p.restore();
}

// GTA 6: three pink → orange bars standing on the text baseline, ending at `right`.
qreal draw_equalizer(QPainter &p, qreal right, qreal baseline, qreal height)
{
	const qreal bar = std::max<qreal>(1.5, height * 0.26);
	const qreal gap = bar * 0.66;
	const qreal heights[] = {0.36, 0.64, 1.0}; // still, in steps (the paused look)
	const qreal width = 3 * bar + 2 * gap;
	QLinearGradient fill(0, baseline, 0, baseline - height);
	fill.setColorAt(0.0, kVicePink);
	fill.setColorAt(1.0, kViceOrange);
	p.save();
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	for (int i = 0; i < 3; ++i) {
		const qreal h = height * heights[i];
		p.drawRoundedRect(QRectF(right - width + i * (bar + gap), baseline - h, bar, h), bar * 0.3, bar * 0.3);
	}
	p.restore();
	return width;
}

void draw_mac_buttons(QPainter &p, qreal left, qreal center_y, qreal u)
{
	p.save();
	p.setPen(Qt::NoPen);
	const QColor colors[] = {QColor(0xff, 0x5f, 0x57), QColor(0xfe, 0xbc, 0x2e), QColor(0x28, 0xc8, 0x40)};
	for (int i = 0; i < 3; ++i) {
		p.setBrush(colors[i]);
		p.drawEllipse(QPointF(left + (6 + 20 * i) * u, center_y), 5.5 * u, 5.5 * u);
	}
	p.restore();
}

QPointF text_origin(const QRectF &rect, const QFontMetricsF &metrics, const QString &shown, Qt::Alignment align)
{
	const qreal x = (align & Qt::AlignRight) ? rect.right() - metrics.horizontalAdvance(shown) : rect.left();
	return QPointF(x, rect.bottom() - metrics.descent());
}

// Text elided to `rect`, bottom aligned, with the style's glow, color split or shadow.
void draw_text_fx(QPainter &p, const QRectF &rect, const QString &text, Qt::Alignment align, const QColor &color,
		  const TextFx &fx, qreal u)
{
	const QFontMetricsF metrics(p.font());
	const QString shown = metrics.elidedText(text, Qt::ElideRight, rect.width());
	const QPointF origin = text_origin(rect, metrics, shown, align);

	if (fx.shadow) {
		p.setPen(rgba(0, 0, 0, 0.25));
		p.drawText(origin + QPointF(0, 2 * u), shown);
		p.setPen(rgba(0, 0, 0, 0.35));
		p.drawText(origin + QPointF(0, u), shown);
	}
	if (fx.glow.isValid() && fx.glow_px > 0) {
		QPainterPath path;
		path.addText(origin, p.font(), shown);
		p.save();
		p.setBrush(Qt::NoBrush);
		for (int i = 4; i >= 1; --i) {
			p.setPen(QPen(with_alpha(fx.glow, fx.glow.alpha() * 22 / 100), fx.glow_px * u * i / 2.5,
				      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			p.drawPath(path);
		}
		p.restore();
	}
	if (fx.chroma_px > 0) {
		const QPointF split(fx.chroma_px * u, 0);
		p.setPen(rgba(255, 40, 80, fx.chroma_alpha));
		p.drawText(origin - split, shown);
		p.setPen(rgba(40, 230, 255, fx.chroma_alpha));
		p.drawText(origin + split, shown);
	}
	p.setPen(color);
	p.drawText(origin, shown);
}

void draw_bar(QPainter &p, const Theme &theme, const QRectF &bar, double progress, qreal u,
	      const CelebrationFrame *celebration)
{
	const QPainterPath track = bar_path(bar, theme.square_bar);
	p.fillPath(track, theme.track);

	const qreal fill_width = bar.width() * std::clamp(progress, 0.0, 1.0);
	if (fill_width > 0.0) {
		// Starting one bar-height to the left keeps the right end rounded at any progress.
		const QRectF fill = theme.square_bar ? QRectF(bar.left(), bar.top(), fill_width, bar.height())
						     : QRectF(bar.left() - bar.height(), bar.top(),
							      fill_width + bar.height(), bar.height());
		const QPainterPath fill_path = bar_path(fill, theme.square_bar).intersected(track);
		if (theme.fill_glow) {
			p.save();
			p.setBrush(Qt::NoBrush);
			for (int i = 3; i >= 1; --i) {
				p.setPen(QPen(with_alpha(theme.accent, 60), 8 * u * i / 3.0, Qt::SolidLine, Qt::RoundCap,
					      Qt::RoundJoin));
				p.drawPath(fill_path);
			}
			p.restore();
		}
		if (theme.fill_end.isValid()) {
			QLinearGradient vice(bar.left(), 0, bar.right(), 0);
			vice.setColorAt(0.0, theme.accent);
			vice.setColorAt(1.0, theme.fill_end);
			p.fillPath(fill_path, vice);
		} else {
			p.fillPath(fill_path, theme.accent);
		}
	}
	if (theme.track_border_w > 0) {
		p.save();
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(theme.track_border, theme.track_border_w * u));
		p.drawPath(track);
		p.restore();
	}
	if (celebration && celebration->active)
		draw_celebration_glow(p, track, bar.height(), theme.accent, celebration->glow);
}

// Everything but the confetti, which stays out of the Glitch jumps.
void draw_card(QPainter &p, const GoalView &view, const BarStyle &style, const Theme &theme,
	       const CardLayout &layout, QSizeF size, const QString &reached_text,
	       const CelebrationFrame *celebration)
{
	const QRectF &card = layout.card;
	const qreal u = layout.u;
	const QPainterPath shape = shape_path(theme, card, u);
	draw_drop_shadow(p, shape, size, layout.margin, u, theme.shadow);
	draw_outer_glow(p, shape, size, layout.margin, theme.outer_glow, theme.outer_glow_px * u);
	draw_background(p, style, shape, card, u);

	const qreal pad_y = 15 * u;
	const QRectF content = card.adjusted(theme.pad_left * u, pad_y, -theme.pad_right * u, -pad_y);
	const qreal bar_height = std::max<qreal>(4.0, content.height() * theme.bar_share);
	const qreal gap = content.height() * 0.1;
	const QRectF bar(content.left(), content.bottom() - bar_height, content.width(), bar_height);
	const QRectF text_row(content.left(), content.top(), content.width(),
			      std::max<qreal>(1.0, bar.top() - gap - content.top()));
	const qreal text_px = text_row.height() * 0.95;

	draw_bar(p, theme, bar, view.progress, u, celebration);

	const QString value = QString::fromStdString(view.value_label);
	QFont value_font = font_with(text_px * theme.value_scale, theme.value_weight, theme.value_families);
	value_font.setLetterSpacing(QFont::PercentageSpacing, theme.value_spacing);
	p.setFont(value_font);
	const QFontMetricsF value_metrics(p.font());
	qreal value_width = std::min(value_metrics.horizontalAdvance(value) + 1.0, text_row.width());
	draw_text_fx(p, text_row, value, Qt::AlignRight, theme.value, theme.value_fx, u);
	if (theme.equalizer) {
		const qreal baseline = text_row.bottom() - value_metrics.descent();
		const qreal gap = value_metrics.capHeight() * 0.35;
		value_width += gap + draw_equalizer(p, text_row.right() - value_width - gap, baseline,
						    value_metrics.capHeight() * 0.8);
	}

	if (style.preset == StylePreset::MacOS) {
		const qreal center_y = text_row.bottom() - value_metrics.descent() - value_metrics.capHeight() / 2;
		draw_mac_buttons(p, card.left() + 14 * u, center_y, u);
	}

	const bool show_reached = view.reached || (celebration && celebration->active);
	QString title = show_reached ? reached_text : QString::fromStdString(view.title);
	if (theme.uppercase_title)
		title = title.toUpper();
	QRectF title_rect = text_row;
	title_rect.setLeft(text_row.left() + theme.title_inset * u);
	title_rect.setRight(text_row.right() - value_width - text_px * 0.6);
	if (title_rect.width() > 0) {
		p.setFont(font_with(text_px * theme.title_scale, theme.title_weight, theme.title_families));
		draw_text_fx(p, title_rect, title, Qt::AlignLeft, show_reached ? theme.reached : theme.title,
			     theme.title_fx, u);
	}
}

} // namespace

void paint_card(QPainter &painter, const GoalView &view, const BarStyle &style, QSizeF size,
		const QString &reached_text, const CelebrationFrame *celebration, double clock_seconds)
{
	const Theme theme = theme_for(style);
	const CardLayout layout = card_layout(size);
	const GlitchFrame glitch = style_animates(style.preset) ? glitch_frame(clock_seconds) : GlitchFrame{};

	if (glitch.clipped()) {
		// The card jumps sideways and only a band of it shows for a moment.
		QImage layer(size.toSize(), QImage::Format_ARGB32_Premultiplied);
		layer.fill(Qt::transparent);
		{
			QPainter layer_painter(&layer);
			layer_painter.setRenderHints(painter.renderHints());
			draw_card(layer_painter, view, style, theme, layout, size, reached_text, celebration);
		}
		const qreal top = layout.card.top() + layout.card.height() * glitch.clip_top;
		const qreal bottom = layout.card.top() + layout.card.height() * glitch.clip_bottom;
		painter.save();
		painter.setClipRect(QRectF(0, top, size.width(), bottom - top));
		painter.drawImage(QPointF(glitch.shift * layout.u, 0), layer);
		painter.restore();
	} else {
		draw_card(painter, view, style, theme, layout, size, reached_text, celebration);
	}

	if (celebration && celebration->active)
		draw_confetti(painter, size, *celebration, theme.accent);
}

} // namespace goalbar::render

#include "main_window.hpp"

#include "core/style_preset.hpp"
#include "render/goal_bar_painter.hpp"
#include "system/providers.hpp"
#include "system/twitch_services.hpp"
#include "translator.hpp"
#include "ui/support_dialog.hpp"
#include "ui/twitch_connect_dialog.hpp"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>

namespace goalbar::app {
namespace {

constexpr int kFrameMs = 33; // about 30 frames per second while something moves

std::unique_ptr<MetricProvider> make_provider(const ProviderKey &key)
{
	const bool has_channel = key.platform == Platform::Twitch || !key.channel.empty();
	if (key.platform == Platform::Manual || !has_channel)
		return nullptr;
	if (metric_from_chat(key.platform, key.metric))
		return sys::make_chat_provider(key.platform, key.metric, key.min_chars, key.channel);
	switch (key.platform) {
	case Platform::YouTube:
		return sys::make_youtube_provider(key.channel, key.metric);
	case Platform::Twitch:
		return sys::make_twitch_provider(key.metric);
	case Platform::Kick:
		return sys::make_kick_provider(key.channel, key.metric);
	case Platform::TikTok:
		return sys::make_tiktok_provider(key.channel, key.metric);
	default:
		return nullptr;
	}
}

const char *style_label(StylePreset preset)
{
	switch (preset) {
	case StylePreset::Bold:
		return "Style.Bold";
	case StylePreset::Neon:
		return "Style.Neon";
	case StylePreset::Glass:
		return "Style.Glass";
	case StylePreset::Pill:
		return "Style.Pill";
	case StylePreset::MacOS:
		return "Style.MacOS";
	case StylePreset::Line:
		return "Style.Line";
	case StylePreset::Synthwave:
		return "Style.Synthwave";
	case StylePreset::NeonCity:
		return "Style.NeonCity";
	case StylePreset::Cyber:
		return "Style.Cyber";
	case StylePreset::Retro:
		return "Style.Retro";
	case StylePreset::Glitch:
		return "Style.Glitch";
	case StylePreset::Gta6:
		return "Style.Gta6";
	case StylePreset::Gta6Disc:
		return "Style.Gta6Disc";
	case StylePreset::Minimal:
	default:
		return "Style.Minimal";
	}
}

const std::pair<Metric, const char *> kMetrics[] = {
	{Metric::Likes, "Metric.Likes"},         {Metric::Viewers, "Metric.Viewers"},
	{Metric::Subscribers, "Metric.Subscribers"}, {Metric::Followers, "Metric.Followers"},
	{Metric::Bits, "Metric.Bits"},           {Metric::Comments, "Metric.Comments"},
	{Metric::PaidSubs, "Metric.PaidSubs"},   {Metric::Diamonds, "Metric.Diamonds"}};

const std::pair<Platform, const char *> kPlatforms[] = {{Platform::Manual, "Platform.Manual"},
							  {Platform::YouTube, "Platform.YouTube"},
							  {Platform::Twitch, "Platform.Twitch"},
							  {Platform::Kick, "Platform.Kick"},
							  {Platform::TikTok, "Platform.TikTok"}};

QColor to_qcolor(uint32_t argb)
{
	return QColor((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

uint32_t from_qcolor(const QColor &color)
{
	return (uint32_t(color.alpha()) << 24) | (uint32_t(color.red()) << 16) | (uint32_t(color.green()) << 8) |
	       uint32_t(color.blue());
}

QSpinBox *count_box()
{
	auto *box = new QSpinBox;
	box->setRange(0, INT_MAX);
	box->setGroupSeparatorShown(true);
	return box;
}

QLabel *notice(const char *key)
{
	auto *label = new QLabel(tr_text(key));
	label->setWordWrap(true);
	label->setStyleSheet("color: palette(placeholder-text);");
	return label;
}

int clamp_int(uint64_t value)
{
	return int(std::min<uint64_t>(value, INT_MAX));
}

// Hides or shows a form row (label and field).
void show_row(QFormLayout *form, QWidget *field, bool visible)
{
	form->setRowVisible(field, visible);
}

} // namespace

MainWindow::MainWindow(QString settings_path, OverlaySettings saved)
	: settings_path_(std::move(settings_path)),
	  fields_(std::move(saved)),
	  frames_(QApplication::applicationVersion().toStdString())
{
	setWindowTitle(tr_text("App.Name"));
	build_ui();
	frame_timer_.setInterval(kFrameMs);
	connect(&frame_timer_, &QTimer::timeout, this, [this] { on_frame(); });
	save_timer_.setSingleShot(true);
	save_timer_.setInterval(1000);
	connect(&save_timer_, &QTimer::timeout, this, [this] { save_now(); });
}

MainWindow::~MainWindow()
{
	frame_timer_.stop();
	save_now();
	frames_.stop();
	engine_.reset(); // joins the provider before the system adapters go away
}

void MainWindow::start()
{
	engine_ = std::make_unique<GoalEngine>(make_provider);
	engine_->configure(fields_, true);
	fields_ = engine_->settings();
	show_goal();
	restart_server();
	clock_.start();
	frame_timer_.start();
	render();
}

// ---- UI --------------------------------------------------------------------

void MainWindow::build_ui()
{
	auto *content = new QWidget;
	auto *column = new QVBoxLayout(content);
	column->addWidget(build_overlay_group());
	column->addWidget(build_goal_group());
	column->addWidget(build_style_group());

	auto *bottom = new QHBoxLayout;
	language_ = new QComboBox;
	language_->addItem(tr_text("App.Language.System"), QString());
	language_->addItem(QStringLiteral("Español"), QStringLiteral("es"));
	language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
	language_->setCurrentIndex(std::max(0, language_->findData(QString::fromStdString(fields_.language))));
	language_->setToolTip(tr_text("App.Language.Restart"));
	connect(language_, &QComboBox::currentIndexChanged, this, [this] {
		fields_.language = language_->currentData().toString().toStdString();
		apply_fields();
	});
	bottom->addWidget(new QLabel(tr_text("App.Language")));
	bottom->addWidget(language_);
	bottom->addStretch();
	auto *support = new QPushButton(QStringLiteral("☕ ") + tr_text("Support.Link"));
	support->setFlat(true);
	connect(support, &QPushButton::clicked, this, [this] { ui::run_support_dialog(this); });
	bottom->addWidget(support);
	column->addLayout(bottom);
	column->addStretch();

	auto *scroll = new QScrollArea;
	scroll->setWidget(content);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->addWidget(scroll);
	resize(640, 860);

	show_visibility();
	show_twitch();
}

QGroupBox *MainWindow::build_overlay_group()
{
	auto *group = new QGroupBox(tr_text("App.Section.Overlay"));
	auto *layout = new QVBoxLayout(group);

	preview_ = new QLabel;
	preview_->setMinimumHeight(110);
	preview_->setAlignment(Qt::AlignCenter);
	preview_->setStyleSheet("background: #202024; border-radius: 6px;");
	layout->addWidget(preview_);

	auto *url_row = new QHBoxLayout;
	url_ = new QLineEdit;
	url_->setReadOnly(true);
	copy_ = new QPushButton(tr_text("App.CopyUrl"));
	connect(copy_, &QPushButton::clicked, this, [this] {
		QApplication::clipboard()->setText(overlay_url());
		copy_->setText(tr_text("App.Copied"));
		QTimer::singleShot(1500, copy_, [this] { copy_->setText(tr_text("App.CopyUrl")); });
	});
	auto *open = new QPushButton(tr_text("App.OpenBrowser"));
	connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(overlay_url())); });
	url_row->addWidget(new QLabel(tr_text("App.OverlayUrl")));
	url_row->addWidget(url_, 1);
	url_row->addWidget(copy_);
	url_row->addWidget(open);
	layout->addLayout(url_row);

	auto *port_row = new QHBoxLayout;
	port_ = new QSpinBox;
	port_->setRange(1024, 65535);
	port_->setValue(fields_.port);
	connect(port_, &QSpinBox::editingFinished, this, [this] {
		if (port_->value() == fields_.port && frames_.running())
			return;
		fields_.port = uint16_t(port_->value());
		apply_fields();
		restart_server();
	});
	server_status_ = new QLabel;
	server_status_->setWordWrap(true);
	port_row->addWidget(new QLabel(tr_text("App.Port")));
	port_row->addWidget(port_);
	port_row->addWidget(server_status_, 1);
	layout->addLayout(port_row);

	help_ = new QLabel;
	help_->setWordWrap(true);
	help_->setStyleSheet("color: palette(placeholder-text);");
	layout->addWidget(help_);
	return group;
}

QGroupBox *MainWindow::build_goal_group()
{
	auto *group = new QGroupBox(tr_text("App.Section.Goal"));
	goal_form_ = new QFormLayout(group);
	QFormLayout *form = goal_form_;

	platform_ = new QComboBox;
	for (const auto &[platform, label] : kPlatforms)
		platform_->addItem(tr_text(label), QString::fromUtf8(platform_id(platform).data()));
	platform_->setCurrentIndex(
		std::max(0, platform_->findData(QString::fromUtf8(platform_id(fields_.platform).data()))));
	form->addRow(tr_text("GoalBar.Platform"), platform_);

	youtube_ = new QLineEdit(QString::fromStdString(fields_.youtube_channel));
	youtube_->setPlaceholderText(QStringLiteral("@handle"));
	form->addRow(tr_text("GoalBar.Channel"), youtube_);
	kick_ = new QLineEdit(QString::fromStdString(fields_.kick_channel));
	form->addRow(tr_text("GoalBar.Kick.Channel"), kick_);
	tiktok_ = new QLineEdit(QString::fromStdString(fields_.tiktok_channel));
	tiktok_->setPlaceholderText(QStringLiteral("@usuario"));
	form->addRow(tr_text("GoalBar.TikTok.Channel"), tiktok_);

	auto *twitch_row = new QWidget;
	auto *twitch_layout = new QHBoxLayout(twitch_row);
	twitch_layout->setContentsMargins(0, 0, 0, 0);
	twitch_status_ = new QLabel;
	twitch_connect_ = new QPushButton(tr_text("GoalBar.Twitch.Connect"));
	twitch_disconnect_ = new QPushButton(tr_text("GoalBar.Twitch.Disconnect"));
	twitch_layout->addWidget(twitch_status_, 1);
	twitch_layout->addWidget(twitch_connect_);
	twitch_layout->addWidget(twitch_disconnect_);
	form->addRow(QStringLiteral("Twitch"), twitch_row);
	connect(twitch_connect_, &QPushButton::clicked, this, [this] {
		ui::run_twitch_connect_dialog(this, sys::twitch_account());
		show_twitch();
	});
	connect(twitch_disconnect_, &QPushButton::clicked, this, [this] {
		sys::twitch_account()->disconnect();
		show_twitch();
	});

	title_ = new QLineEdit(QString::fromStdString(fields_.title));
	title_->setPlaceholderText(tr_text("GoalBar.DefaultTitle"));
	form->addRow(tr_text("GoalBar.Title"), title_);

	metric_ = new QComboBox;
	form->addRow(tr_text("GoalBar.Metric"), metric_);
	fill_metrics();

	subs_notice_ = notice("GoalBar.SubsNotice");
	form->addRow(subs_notice_);
	min_chars_ = new QSpinBox;
	min_chars_->setRange(1, kMaxMinChars);
	min_chars_->setValue(fields_.min_chars);
	min_chars_->setToolTip(tr_text("GoalBar.ChatMinCharsHelp"));
	form->addRow(tr_text("GoalBar.ChatMinChars"), min_chars_);
	paid_notice_ = notice("GoalBar.PaidNotice");
	form->addRow(paid_notice_);
	chat_notice_ = notice("GoalBar.ChatNotice");
	form->addRow(chat_notice_);
	tiktok_notice_ = notice("GoalBar.TikTok.Notice");
	form->addRow(tiktok_notice_);

	current_ = count_box();
	form->addRow(tr_text("GoalBar.Current"), current_);
	target_ = count_box();
	target_->setMinimum(1);
	form->addRow(tr_text("GoalBar.Target"), target_);
	status_ = new QLabel;
	status_->setStyleSheet("color: palette(placeholder-text);");
	form->addRow(status_);

	celebrate_ = new QCheckBox(tr_text("GoalBar.Actions.Celebrate"));
	celebrate_->setChecked(fields_.celebrate);
	form->addRow(celebrate_);
	auto_next_ = new QCheckBox(tr_text("GoalBar.Actions.AutoNext"));
	auto_next_->setChecked(fields_.auto_next);
	form->addRow(auto_next_);
	step_ = count_box();
	step_->setMinimum(1);
	step_->setValue(clamp_int(fields_.step));
	form->addRow(tr_text("GoalBar.Actions.Step"), step_);

	auto *buttons = new QHBoxLayout;
	auto *test = new QPushButton(tr_text("GoalBar.Actions.Test"));
	connect(test, &QPushButton::clicked, this, [this] {
		engine_->test_celebration();
		repaint_ = true;
	});
	auto *reset = new QPushButton(tr_text("GoalBar.ResetGoal"));
	connect(reset, &QPushButton::clicked, this, [this] {
		engine_->reset_goal();
		repaint_ = true;
	});
	buttons->addWidget(test);
	buttons->addWidget(reset);
	buttons->addStretch();
	form->addRow(buttons);

	// Any edit applies at once.
	connect(platform_, &QComboBox::currentIndexChanged, this, [this] {
		fill_metrics();
		show_visibility();
		show_twitch();
		apply_fields();
	});
	connect(metric_, &QComboBox::currentIndexChanged, this, [this] {
		show_visibility();
		apply_fields();
	});
	for (QLineEdit *edit : {youtube_, kick_, tiktok_})
		connect(edit, &QLineEdit::editingFinished, this, [this] { apply_fields(); });
	connect(title_, &QLineEdit::textChanged, this, [this] { apply_fields(); });
	for (QSpinBox *box : {min_chars_, current_, target_, step_})
		connect(box, &QSpinBox::valueChanged, this, [this] { apply_fields(); });
	for (QCheckBox *box : {celebrate_, auto_next_})
		connect(box, &QCheckBox::toggled, this, [this] {
			show_visibility();
			apply_fields();
		});
	return group;
}

QGroupBox *MainWindow::build_style_group()
{
	auto *group = new QGroupBox(tr_text("App.Section.Style"));
	style_form_ = new QFormLayout(group);
	QFormLayout *form = style_form_;

	style_ = new QComboBox;
	for (StylePreset preset : kStylePresets)
		style_->addItem(tr_text(style_label(preset)), QString::fromUtf8(style_preset_id(preset).data()));
	style_->setCurrentIndex(std::max(0, style_->findData(QString::fromStdString(fields_.style))));
	style_->setToolTip(tr_text("GoalBar.StyleHelp"));
	form->addRow(tr_text("GoalBar.Style"), style_);

	accent_ = new QPushButton;
	paint_color_button(accent_, fields_.accent);
	connect(accent_, &QPushButton::clicked, this, [this] { pick_color(fields_.accent, accent_); });
	form->addRow(tr_text("GoalBar.AccentColor"), accent_);
	text_color_ = new QPushButton;
	paint_color_button(text_color_, fields_.text_color);
	connect(text_color_, &QPushButton::clicked, this, [this] { pick_color(fields_.text_color, text_color_); });
	form->addRow(tr_text("GoalBar.TextColor"), text_color_);

	opacity_ = new QSlider(Qt::Horizontal);
	opacity_->setRange(0, 100);
	opacity_->setSingleStep(5);
	opacity_->setValue(fields_.opacity);
	form->addRow(tr_text("GoalBar.Opacity"), opacity_);

	width_ = new QSpinBox;
	width_->setRange(16, 4096);
	width_->setValue(int(fields_.width));
	form->addRow(tr_text("GoalBar.Width"), width_);
	height_ = new QSpinBox;
	height_->setRange(16, 4096);
	height_->setValue(int(fields_.height));
	form->addRow(tr_text("GoalBar.Height"), height_);

	connect(style_, &QComboBox::currentIndexChanged, this, [this] {
		show_visibility();
		apply_fields();
	});
	connect(opacity_, &QSlider::valueChanged, this, [this] { apply_fields(); });
	for (QSpinBox *box : {width_, height_})
		connect(box, &QSpinBox::valueChanged, this, [this] { apply_fields(); });
	return group;
}

void MainWindow::paint_color_button(QPushButton *button, uint32_t color)
{
	const QColor qcolor = to_qcolor(color);
	button->setText(qcolor.name(QColor::HexArgb).toUpper());
	button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; padding: 4px 10px; }")
				      .arg(qcolor.name(QColor::HexRgb),
					   qcolor.lightness() > 140 ? QStringLiteral("#000") : QStringLiteral("#fff")));
}

void MainWindow::pick_color(uint32_t &color, QPushButton *button)
{
	const QColor chosen =
		QColorDialog::getColor(to_qcolor(color), this, tr_text("App.ChooseColor"), QColorDialog::ShowAlphaChannel);
	if (!chosen.isValid())
		return;
	color = from_qcolor(chosen);
	paint_color_button(button, color);
	apply_fields();
}

void MainWindow::fill_metrics()
{
	const QSignalBlocker block(metric_);
	const Platform platform =
		platform_from_id(platform_->currentData().toString().toStdString()).value_or(Platform::Manual);
	const Metric wanted = readable_metric(platform, fields_.metric);
	metric_->clear();
	for (const auto &[metric, label] : kMetrics) {
		if (platform_reads(platform, metric))
			metric_->addItem(tr_text(label), QString::fromUtf8(metric_id(metric).data()));
	}
	metric_->setCurrentIndex(std::max(0, metric_->findData(QString::fromUtf8(metric_id(wanted).data()))));
}

void MainWindow::show_visibility()
{
	const Platform platform =
		platform_from_id(platform_->currentData().toString().toStdString()).value_or(Platform::Manual);
	const Metric metric = metric_from_id(metric_->currentData().toString().toStdString()).value_or(Metric::Likes);
	const bool from_chat = metric_from_chat(platform, metric);
	show_row(goal_form_, youtube_, platform == Platform::YouTube);
	show_row(goal_form_, kick_, platform == Platform::Kick);
	show_row(goal_form_, tiktok_, platform == Platform::TikTok);
	show_row(goal_form_, twitch_status_->parentWidget(), platform == Platform::Twitch);
	show_row(goal_form_, subs_notice_, platform == Platform::YouTube && metric == Metric::Subscribers);
	show_row(goal_form_, min_chars_, metric == Metric::Comments);
	show_row(goal_form_, paid_notice_, metric == Metric::PaidSubs);
	show_row(goal_form_, chat_notice_, from_chat && platform != Platform::TikTok);
	show_row(goal_form_, tiktok_notice_, from_chat && platform == Platform::TikTok);
	// Typed in manual mode; adjustable for counted metrics (the chat adds to it).
	show_row(goal_form_, current_, platform == Platform::Manual || metric_is_counted(metric));
	show_row(goal_form_, step_, auto_next_->isChecked());
	const StylePreset preset = style_preset_from_id(style_->currentData().toString().toStdString());
	show_row(style_form_, accent_, style_uses_custom_colors(preset));
	show_row(style_form_, text_color_, style_uses_custom_colors(preset));
	show_row(style_form_, opacity_, style_has_card(preset));
}

void MainWindow::show_twitch()
{
	const bool twitch = platform_->currentData().toString() == QStringLiteral("twitch");
	if (!twitch)
		return;
	const TwitchAccount::Status account = sys::twitch_account()->status();
	QString line = tr_text("GoalBar.Twitch.NotConnected");
	if (account.connected) {
		line = account.display_name.empty()
			       ? tr_text("GoalBar.Twitch.Connected")
			       : tr_text("GoalBar.Twitch.ConnectedAs") + QStringLiteral(" ") +
					 QString::fromStdString(account.display_name);
	}
	twitch_status_->setText(line);
	twitch_connect_->setVisible(!account.connected);
	twitch_disconnect_->setVisible(account.connected);
}

void MainWindow::apply_fields()
{
	if (loading_ || !engine_)
		return;
	OverlaySettings next = engine_->settings(); // keeps the runtime goal
	next.platform =
		platform_from_id(platform_->currentData().toString().toStdString()).value_or(Platform::Manual);
	next.metric = metric_from_id(metric_->currentData().toString().toStdString()).value_or(Metric::Likes);
	next.youtube_channel = youtube_->text().trimmed().toStdString();
	next.kick_channel = kick_->text().trimmed().toStdString();
	next.tiktok_channel = tiktok_->text().trimmed().toStdString();
	next.title = title_->text().toStdString();
	next.min_chars = min_chars_->value();
	if (current_->isVisibleTo(this) && uint64_t(current_->value()) != std::min<uint64_t>(next.goal.current, INT_MAX))
		next.goal.current = uint64_t(current_->value());
	if (uint64_t(target_->value()) != std::min<uint64_t>(next.goal.target, INT_MAX))
		next.goal.target = uint64_t(target_->value());
	next.celebrate = celebrate_->isChecked();
	next.auto_next = auto_next_->isChecked();
	next.step = uint64_t(step_->value());
	next.style = style_->currentData().toString().toStdString();
	next.accent = fields_.accent;
	next.text_color = fields_.text_color;
	next.opacity = opacity_->value();
	next.width = uint32_t(width_->value());
	next.height = uint32_t(height_->value());
	next.port = fields_.port;
	next.language = fields_.language;
	engine_->configure(next);
	fields_ = engine_->settings();
	show_goal(); // switching to a counted metric may have reset the value
	repaint_ = true;
	save_timer_.start();
}

void MainWindow::show_goal()
{
	loading_ = true;
	const Goal &goal = engine_->settings().goal;
	if (current_->value() != clamp_int(goal.current))
		current_->setValue(clamp_int(goal.current));
	if (target_->value() != clamp_int(goal.target))
		target_->setValue(clamp_int(goal.target));
	const std::string &key = engine_->status();
	const QString status = key.empty() ? QString() : tr_text(key.c_str());
	if (status_->text() != status) {
		status_->setText(status);
		status_->setVisible(!key.empty());
	}
	loading_ = false;
}

// ---- overlay ---------------------------------------------------------------

QString MainWindow::overlay_url() const
{
	return QStringLiteral("http://127.0.0.1:%1%2").arg(frames_.port()).arg(QString::fromUtf8(kOverlayPath.data()));
}

void MainWindow::restart_server()
{
	std::string error;
	if (frames_.start(fields_.port, error)) {
		server_error_.clear();
	} else {
		server_error_ = error.find("in use") != std::string::npos
					? tr_text("App.PortBusy").arg(fields_.port)
					: tr_text("App.ServerOff").arg(QString::fromStdString(error));
	}
	repaint_ = true; // the new server has no frame yet
	shown_viewers_ = -1;
	show_server();
}

void MainWindow::show_server()
{
	url_->setText(frames_.running() ? overlay_url() : QString());
	copy_->setEnabled(frames_.running());
	if (!server_error_.isEmpty()) {
		server_status_->setText(server_error_);
		server_status_->setStyleSheet("color: #e5484d;");
	} else {
		const int viewers = frames_.viewers();
		server_status_->setText(viewers > 0 ? tr_text("App.Viewers").arg(viewers) : tr_text("App.NotShown"));
		server_status_->setStyleSheet(viewers > 0 ? "color: #30a46c;" : "color: palette(placeholder-text);");
	}
	help_->setText(tr_text("App.Help").arg(fields_.width).arg(fields_.height));
}

void MainWindow::on_frame()
{
	const double seconds = std::min(0.25, clock_.restart() / 1000.0);
	if (engine_->tick(seconds))
		repaint_ = true;
	if (engine_->take_goal_changed()) {
		fields_ = engine_->settings();
		save_timer_.start();
	}
	show_goal();
	const int viewers = frames_.viewers();
	if (viewers != shown_viewers_) {
		shown_viewers_ = viewers;
		show_server();
	}
	if (repaint_)
		render();
}

void MainWindow::render()
{
	repaint_ = false;
	const OverlaySettings &s = engine_->settings();
	const GoalEngine::Frame frame =
		engine_->frame([](const std::string &key) { return Translator::instance().utf8(key); });
	GoalView view = frame.view;
	if (view.title.empty())
		view.title = tr_text("GoalBar.DefaultTitle").toStdString();
	render::BarStyle style;
	style.preset = style_preset_from_id(s.style);
	style.accent = to_qcolor(s.accent);
	style.text = to_qcolor(s.text_color);
	style.opacity = s.opacity;
	last_frame_ = render::GoalBarPainter::paint(view, style, QSize(int(s.width), int(s.height)),
						    tr_text("GoalBar.Reached"),
						    frame.celebration ? &*frame.celebration : nullptr, frame.style_clock);

	QByteArray png;
	QBuffer buffer(&png);
	buffer.open(QIODevice::WriteOnly);
	last_frame_.save(&buffer, "PNG", 80); // light compression: it travels over loopback
	frames_.publish(png.toStdString());

	if (isVisible()) {
		const QSize box = preview_->contentsRect().size() - QSize(16, 16);
		if (box.width() > 0 && box.height() > 0)
			preview_->setPixmap(QPixmap::fromImage(
				last_frame_.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
	}
}

void MainWindow::save_now()
{
	if (!engine_)
		return;
	QSaveFile file(settings_path_);
	if (!file.open(QIODevice::WriteOnly))
		return;
	const std::string json = settings_to_json(engine_->settings());
	file.write(json.data(), qint64(json.size()));
	file.commit();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	// Closing the window keeps the overlay running from the tray.
	if (tray_ && tray_->isVisible()) {
		hide();
		event->ignore();
		if (!told_tray_) {
			told_tray_ = true;
			tray_->showMessage(tr_text("App.Name"), tr_text("App.Tray.Hidden"));
		}
		return;
	}
	QWidget::closeEvent(event);
}

} // namespace goalbar::app

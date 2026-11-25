// Overlay app: the settings window (the same fields as the plugin's Goal Bar
// properties), the overlay URL for Meld Studio and a live preview. It owns the
// goal engine and the local web server, and paints the frames.
#pragma once

#include "app/frame_service.hpp"
#include "app/goal_engine.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <memory>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QSystemTrayIcon;
class QFormLayout;

namespace goalbar::app {

class MainWindow final : public QWidget {
public:
	MainWindow(QString settings_path, OverlaySettings saved);
	~MainWindow() override;

	// Starts the engine, the web server and the frame timer.
	void start();
	void set_tray(QSystemTrayIcon *tray) { tray_ = tray; }

protected:
	void closeEvent(QCloseEvent *event) override;

private:
	void build_ui();
	QGroupBox *build_goal_group();
	QGroupBox *build_style_group();
	QGroupBox *build_overlay_group();

	// Fields → settings → engine.
	void apply_fields();
	// Engine → fields (after a reading moved the goal).
	void show_goal();
	void show_visibility();
	void fill_metrics();
	void show_twitch();
	void show_server();
	void restart_server();
	void pick_color(uint32_t &color, QPushButton *button);
	void paint_color_button(QPushButton *button, uint32_t color);

	void on_frame();
	void render();
	void save_now();

	QString overlay_url() const;

	const QString settings_path_;
	OverlaySettings fields_; // what the window shows (style, size, channels…)
	std::unique_ptr<GoalEngine> engine_;
	FrameService frames_;
	QString server_error_;

	QTimer frame_timer_;
	QTimer save_timer_;
	QElapsedTimer clock_;
	bool repaint_ = true;
	int shown_viewers_ = -1;
	bool loading_ = false; // filling fields programmatically
	QImage last_frame_;
	QSystemTrayIcon *tray_ = nullptr;
	bool told_tray_ = false;

	QComboBox *platform_ = nullptr;
	QLineEdit *youtube_ = nullptr;
	QLineEdit *kick_ = nullptr;
	QLineEdit *tiktok_ = nullptr;
	QLabel *twitch_status_ = nullptr;
	QPushButton *twitch_connect_ = nullptr;
	QPushButton *twitch_disconnect_ = nullptr;
	QLineEdit *title_ = nullptr;
	QComboBox *metric_ = nullptr;
	QSpinBox *min_chars_ = nullptr;
	QLabel *subs_notice_ = nullptr;
	QLabel *paid_notice_ = nullptr;
	QLabel *chat_notice_ = nullptr;
	QLabel *tiktok_notice_ = nullptr;
	QSpinBox *current_ = nullptr;
	QSpinBox *target_ = nullptr;
	QCheckBox *celebrate_ = nullptr;
	QCheckBox *auto_next_ = nullptr;
	QSpinBox *step_ = nullptr;
	QLabel *status_ = nullptr;

	QComboBox *style_ = nullptr;
	QPushButton *accent_ = nullptr;
	QPushButton *text_color_ = nullptr;
	QSlider *opacity_ = nullptr;
	QSpinBox *width_ = nullptr;
	QSpinBox *height_ = nullptr;

	QLineEdit *url_ = nullptr;
	QPushButton *copy_ = nullptr;
	QSpinBox *port_ = nullptr;
	QLabel *server_status_ = nullptr;
	QLabel *help_ = nullptr;
	QLabel *preview_ = nullptr;
	QComboBox *language_ = nullptr;

	QFormLayout *goal_form_ = nullptr;
	QFormLayout *style_form_ = nullptr;
};

} // namespace goalbar::app

// AC Goal Bar Overlay: the Goal Bar for Meld Studio (or any app with a
// browser source). It reads the platforms the way the OBS plugin does and
// serves the bar at http://127.0.0.1:<port>/goal-bar.
#include "app/overlay_settings.hpp"
#include "main_window.hpp"
#include "system/log.hpp"
#include "translator.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTextStream>

#include <mutex>

namespace {

using namespace goalbar;

// data/ next to the executable (Windows, Linux) or in the bundle's Resources (macOS).
QString data_dir()
{
	const QString base = QCoreApplication::applicationDirPath();
	for (const QString &candidate : {base + "/data", base + "/../Resources/data"}) {
		if (QDir(candidate).exists())
			return QDir(candidate).canonicalPath();
	}
	return base + "/data";
}

void load_fonts(const QString &data)
{
	for (const char *file : {"VT323-Regular.ttf", "MrDafoe-Regular.ttf", "Orbitron-VariableFont_wght.ttf",
				 "Anton-Regular.ttf"})
		QFontDatabase::addApplicationFont(data + "/fonts/" + file);
}

// A small log file next to the settings, rewritten at each start.
void open_log(const QString &folder)
{
	static QFile file(folder + "/ac-goal-bar-overlay.log");
	static std::mutex mutex;
	file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
	sys::set_log_sink([](const std::string &message) {
		std::lock_guard lock(mutex);
		if (!file.isOpen())
			return;
		QTextStream out(&file);
		out << QDateTime::currentDateTime().toString(Qt::ISODate) << ' ' << QString::fromStdString(message)
		    << '\n';
		out.flush();
	});
}

// The tray icon: a small progress bar.
QIcon app_icon()
{
	QIcon icon;
	for (int size : {16, 32, 64, 128, 256}) {
		QPixmap pixmap(size, size);
		pixmap.fill(Qt::transparent);
		QPainter p(&pixmap);
		p.setRenderHint(QPainter::Antialiasing);
		const qreal r = size * 0.18;
		QPainterPath card;
		card.addRoundedRect(QRectF(0, 0, size, size), r, r);
		p.fillPath(card, QColor(24, 24, 30));
		const QRectF track(size * 0.14, size * 0.42, size * 0.72, size * 0.18);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 50));
		p.drawRoundedRect(track, track.height() / 2, track.height() / 2);
		QRectF fill = track;
		fill.setWidth(track.width() * 0.68);
		p.setBrush(QColor(255, 61, 110));
		p.drawRoundedRect(fill, fill.height() / 2, fill.height() / 2);
		icon.addPixmap(pixmap);
	}
	return icon;
}

} // namespace

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("AC Goal Bar Overlay"));
	QApplication::setOrganizationName(QStringLiteral("alejoxcode"));
	QApplication::setApplicationVersion(QStringLiteral(GOAL_BAR_OVERLAY_VERSION));
	QApplication::setQuitOnLastWindowClosed(false);
	QApplication::setWindowIcon(app_icon());

	const QString config = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	QDir().mkpath(config);
	const QString settings_path = config + "/settings.json";
	app::OverlaySettings settings;
	{
		QFile file(settings_path);
		if (file.open(QIODevice::ReadOnly))
			settings = app::settings_from_json(file.readAll().toStdString());
	}

	const QString data = data_dir();
	app::Translator::instance().load(data, settings.language);
	if (settings.title.empty() && !QFile::exists(settings_path))
		settings.title = app::tr_text("GoalBar.DefaultTitle").toStdString();

	// One app at a time: a second one could not use the same port anyway.
	QLockFile lock(QDir::tempPath() + "/ac-goal-bar-overlay.lock");
	lock.setStaleLockTime(0);
	if (!lock.tryLock(200)) {
		QMessageBox::information(nullptr, app::tr_text("App.Name"), app::tr_text("App.AlreadyRunning"));
		return 0;
	}

	open_log(config);
	sys::log("AC Goal Bar Overlay " GOAL_BAR_OVERLAY_VERSION " started");
	load_fonts(data);

	app::MainWindow window(settings_path, settings);
	QSystemTrayIcon tray(app_icon());
	if (QSystemTrayIcon::isSystemTrayAvailable()) {
		auto *menu = new QMenu(&window);
		menu->addAction(app::tr_text("App.Tray.Open"), &window, [&window] {
			window.showNormal();
			window.raise();
			window.activateWindow();
		});
		menu->addSeparator();
		menu->addAction(app::tr_text("App.Tray.Quit"), &app, &QApplication::quit);
		tray.setContextMenu(menu);
		tray.setToolTip(app::tr_text("App.Name"));
		QObject::connect(&tray, &QSystemTrayIcon::activated, &window, [&window](QSystemTrayIcon::ActivationReason reason) {
			if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
				window.showNormal();
				window.raise();
				window.activateWindow();
			}
		});
		tray.show();
		window.set_tray(&tray);
	} else {
		QApplication::setQuitOnLastWindowClosed(true);
	}
	window.start();
	window.show();
	const int code = app.exec();
	sys::log("AC Goal Bar Overlay closed");
	return code;
}

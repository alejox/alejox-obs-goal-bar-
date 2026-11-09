// Qt UI for updates: the Tools menu action "Goal Bar: Check for updates", the startup check
// (at most once a day), the "new version" dialog and the notices that follow it. Owns the
// update service. UI thread only.
#pragma once

#include "update/update_service.hpp"
#include "update/update_settings.hpp"

#include <QObject>
#include <QPointer>

#include <filesystem>
#include <memory>

class QWidget;

namespace goalbar::ui {

class UpdateController final : public QObject {
	Q_OBJECT

public:
	UpdateController(update::UpdateServiceConfig config, std::filesystem::path settings_path);
	~UpdateController() override;

	// Adds the Tools menu action and schedules the startup check after `delay_ms`, so OBS
	// finishes starting first. Without a due network check it only restores a package staged
	// by an earlier session.
	void start(int delay_ms);

	// Called once as OBS closes; see UpdateService::launch_staged_install.
	bool launch_staged_install();

private:
	void check_now(bool manual);
	void on_state_changed();
	void offer(bool installable);
	void notify(bool warning, const QString &text);
	void save_settings();

	std::unique_ptr<update::UpdateService> service_;
	std::filesystem::path settings_path_;
	update::UpdateSettings settings_;
	// The running check was started from the Tools menu.
	bool manual_ = false;
	// The user accepted the update and the download is ours to report.
	bool installing_ = false;
	QPointer<QWidget> open_dialog_;
};

} // namespace goalbar::ui

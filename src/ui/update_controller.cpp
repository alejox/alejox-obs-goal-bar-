#include "ui/update_controller.hpp"

#include "update/update_settings_file.hpp"

// obs-module.h before plugin-support.h: the other order redefines blogva on MSVC.
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <utility>

namespace goalbar::ui {
namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QWidget *main_window()
{
	return static_cast<QWidget *>(obs_frontend_get_main_window());
}

std::int64_t unix_now()
{
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
		.count();
}

QString link(const std::string &url, const QString &label)
{
	return QStringLiteral("<a href=\"%1\">%2</a>")
		.arg(QString::fromStdString(url).toHtmlEscaped(), label.toHtmlEscaped());
}

// A non-modal dialog on the OBS main window with a rich-text message whose links open in
// the browser. The caller adds the buttons and opens it.
QDialog *make_dialog(const QString &message, QDialogButtonBox *&buttons)
{
	auto *dialog = new QDialog(main_window());
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(text("Update.Title"));

	auto *label = new QLabel(message, dialog);
	label->setTextFormat(Qt::RichText);
	label->setTextInteractionFlags(Qt::TextBrowserInteraction);
	label->setOpenExternalLinks(true);
	label->setWordWrap(true);

	buttons = new QDialogButtonBox(dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::close);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);

	auto *layout = new QVBoxLayout(dialog);
	layout->addWidget(label);
	layout->addWidget(buttons);
	return dialog;
}

} // namespace

UpdateController::UpdateController(update::UpdateServiceConfig config, std::filesystem::path settings_path)
	: service_(std::make_unique<update::UpdateService>(std::move(config))),
	  settings_path_(std::move(settings_path)),
	  settings_(update::load_update_settings(settings_path_))
{
	connect(service_.get(), &update::UpdateService::state_changed, this, &UpdateController::on_state_changed);
}

UpdateController::~UpdateController()
{
	if (open_dialog_) {
		open_dialog_->close();
	}
}

void UpdateController::start(int delay_ms)
{
	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("Update.Menu")));
	if (action) {
		connect(action, &QAction::triggered, this, [this] { check_now(true); });
	}
	QTimer::singleShot(delay_ms, this, [this] { check_now(false); });
}

bool UpdateController::launch_staged_install()
{
	return service_->launch_staged_install();
}

void UpdateController::check_now(bool manual)
{
	if (service_->busy()) {
		if (manual) {
			notify(false, text("Update.Busy").toHtmlEscaped());
		}
		return;
	}
	const bool network = manual || update::automatic_check_due(settings_, unix_now());
	if (network) {
		settings_.last_check = unix_now();
		save_settings();
	} else {
		obs_log(LOG_INFO, "automatic update check skipped (off, or less than a day since the last one)");
	}
	manual_ = manual;
	installing_ = false;
	service_->check(network);
}

void UpdateController::on_state_changed()
{
	const update::UpdateState &state = service_->state();
	const QString version = QString::fromStdString(state.version);
	switch (state.status) {
	case update::UpdateStatus::Idle:
	case update::UpdateStatus::Checking:
	case update::UpdateStatus::Downloading:
		return;
	case update::UpdateStatus::UpToDate:
		if (manual_) {
			notify(false, text("Update.UpToDate").arg(QString::fromUtf8(PLUGIN_VERSION)).toHtmlEscaped());
		}
		break;
	case update::UpdateStatus::CheckFailed:
		if (manual_) {
			notify(true, text("Update.CheckFailed").toHtmlEscaped());
		}
		break;
	case update::UpdateStatus::Available:
	case update::UpdateStatus::Manual:
		if (update::should_offer(settings_, state.version, manual_)) {
			offer(state.status == update::UpdateStatus::Available);
		}
		break;
	case update::UpdateStatus::Ready:
		// A package staged by an earlier session installs on this close without asking again.
		if (manual_ || installing_) {
			notify(false, text("Update.Ready").arg(version).toHtmlEscaped());
		}
		break;
	case update::UpdateStatus::DownloadFailed:
		if (installing_) {
			notify(true, text("Update.DownloadFailed").toHtmlEscaped() + QStringLiteral("<br>") +
					     link(state.release_url, text("Update.OpenPage")));
		}
		break;
	}
	manual_ = false;
	installing_ = false;
}

void UpdateController::offer(bool installable)
{
	if (open_dialog_) {
		open_dialog_->close();
	}
	const update::UpdateState &state = service_->state();
	const std::string offered_version = state.version;

	QString message = text("Update.Available")
				  .arg(QString::fromStdString(state.version).toHtmlEscaped(),
				       QString::fromUtf8(PLUGIN_VERSION).toHtmlEscaped());
	message += QStringLiteral("<br>") + link(state.release_url, text("Update.ReleaseNotes"));
	if (!installable) {
		message += QStringLiteral("<br><br>") + text("Update.ManualOnly").toHtmlEscaped();
	}
	QDialogButtonBox *buttons = nullptr;
	QDialog *dialog = make_dialog(message, buttons);
	if (installable) {
		QPushButton *install = buttons->addButton(text("Update.Install"), QDialogButtonBox::AcceptRole);
		install->setDefault(true);
		connect(install, &QPushButton::clicked, this, [this, offered_version] {
			if (service_->state().status == update::UpdateStatus::Available &&
			    service_->state().version == offered_version) {
				installing_ = true;
				service_->install();
			}
		});
	}
	buttons->addButton(text("Update.Later"), QDialogButtonBox::RejectRole);
	QPushButton *skip = buttons->addButton(text("Update.Skip"), QDialogButtonBox::DestructiveRole);
	connect(skip, &QPushButton::clicked, this, [this, offered_version] {
		settings_.skipped_version = offered_version;
		save_settings();
	});
	connect(skip, &QPushButton::clicked, dialog, &QDialog::close);

	open_dialog_ = dialog;
	dialog->open();
}

void UpdateController::notify(bool warning, const QString &message)
{
	QDialogButtonBox *buttons = nullptr;
	QDialog *dialog = make_dialog(message, buttons);
	buttons->addButton(QDialogButtonBox::Ok);
	if (warning) {
		obs_log(LOG_WARNING, "%s", message.toUtf8().constData());
	}
	dialog->open();
}

void UpdateController::save_settings()
{
	if (const auto problem = update::save_update_settings(settings_path_, settings_)) {
		obs_log(LOG_WARNING, "update settings not saved: %s", problem->c_str());
	}
}

} // namespace goalbar::ui

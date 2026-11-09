#include "ui/twitch_connect_dialog.hpp"

#include <obs-module.h>

#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace goalbar::ui {
namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

class TwitchConnectDialog final : public QDialog {
public:
	TwitchConnectDialog(QWidget *parent, std::shared_ptr<TwitchAccount> account)
		: QDialog(parent),
		  account_(std::move(account))
	{
		setWindowTitle(text("TwitchConnect.Title"));
		build_layout();
		// After the layout exists: the worker posts back into it.
		start_flow();
	}

	~TwitchConnectDialog() override
	{
		// Aborts the request or wait in flight; the worker then ends promptly.
		cancel_->cancel();
		if (worker_.joinable())
			worker_.join();
	}

	TwitchConnectDialog(const TwitchConnectDialog &) = delete;
	TwitchConnectDialog &operator=(const TwitchConnectDialog &) = delete;

	void reject() override
	{
		cancel_->cancel();
		QDialog::reject();
	}

private:
	void build_layout()
	{
		auto *instructions = new QLabel(text("TwitchConnect.Instructions"), this);
		instructions->setWordWrap(true);

		code_ = new QLabel(QStringLiteral("…"), this);
		QFont code_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		code_font.setPointSize(28);
		code_font.setBold(true);
		code_->setFont(code_font);
		code_->setAlignment(Qt::AlignCenter);
		code_->setTextInteractionFlags(Qt::TextSelectableByMouse);

		copy_ = new QPushButton(text("TwitchConnect.CopyCode"), this);
		open_ = new QPushButton(text("TwitchConnect.Open"), this);
		copy_->setEnabled(false);
		open_->setEnabled(false);
		connect(copy_, &QPushButton::clicked, this, [this] {
			QGuiApplication::clipboard()->setText(code_->text());
			set_status(text("TwitchConnect.Copied"));
		});
		connect(open_, &QPushButton::clicked, this,
			[this] { QDesktopServices::openUrl(QUrl(verification_uri_)); });

		auto *actions = new QHBoxLayout();
		actions->addWidget(copy_);
		actions->addWidget(open_);

		status_ = new QLabel(this);
		status_->setWordWrap(true);

		retry_ = new QPushButton(text("TwitchConnect.TryAgain"), this);
		retry_->hide();
		close_ = new QPushButton(text("TwitchConnect.Cancel"), this);
		connect(retry_, &QPushButton::clicked, this, [this] { start_flow(); });
		connect(close_, &QPushButton::clicked, this, &TwitchConnectDialog::reject);

		auto *buttons = new QHBoxLayout();
		buttons->addStretch();
		buttons->addWidget(retry_);
		buttons->addWidget(close_);

		auto *layout = new QVBoxLayout(this);
		layout->addWidget(instructions);
		layout->addWidget(code_);
		layout->addLayout(actions);
		layout->addWidget(status_);
		layout->addLayout(buttons);
		setMinimumWidth(380);
	}

	void set_status(const QString &message) { status_->setText(message); }

	// Runs `fn` on the UI thread; dropped if the dialog is gone by then.
	template<typename Fn> void post(Fn fn) { QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection); }

	// Starts (or restarts) the device flow on a fresh worker. UI thread.
	void start_flow()
	{
		// A restart only happens after the previous flow ended: its worker
		// posted its last message and is returning.
		if (worker_.joinable())
			worker_.join();
		cancel_ = std::make_unique<CancelToken>();
		code_->setText(QStringLiteral("…"));
		copy_->setEnabled(false);
		open_->setEnabled(false);
		retry_->hide();
		close_->setText(text("TwitchConnect.Cancel"));
		set_status(text("TwitchConnect.Requesting"));
		worker_ = std::thread([this, cancel = cancel_.get()] { run(*cancel); });
	}

	// The flow ended without a connection: offer a new code.
	void offer_retry()
	{
		copy_->setEnabled(false);
		open_->setEnabled(false);
		close_->setText(text("TwitchConnect.Close"));
		retry_->show();
		retry_->setDefault(true);
	}

	// Worker thread.
	void run(CancelToken &cancel)
	{
		const TwitchAccount::DeviceStart start = account_->start_connect(cancel);
		if (!start.code) {
			const QString error = QString::fromStdString(start.error);
			post([this, error] {
				set_status(text("TwitchConnect.Failed").arg(error));
				offer_retry();
			});
			return;
		}
		// Only the user code and the URL reach the UI; never the device code.
		const QString user_code = QString::fromStdString(start.code->user_code);
		const QString uri = QString::fromStdString(start.code->verification_uri);
		post([this, user_code, uri] { show_code(user_code, uri); });

		std::string error;
		const TwitchAccount::ConnectResult result = account_->finish_connect(*start.code, cancel, error);
		const QString detail = QString::fromStdString(error);
		post([this, result, detail] { finish(result, detail); });
	}

	void show_code(const QString &user_code, const QString &uri)
	{
		code_->setText(user_code);
		verification_uri_ = uri;
		copy_->setEnabled(true);
		open_->setEnabled(true);
		set_status(text("TwitchConnect.Waiting"));
	}

	void finish(TwitchAccount::ConnectResult result, const QString &detail)
	{
		switch (result) {
		case TwitchAccount::ConnectResult::Connected:
			accept();
			return;
		case TwitchAccount::ConnectResult::Expired:
			set_status(text("TwitchConnect.Expired"));
			break;
		case TwitchAccount::ConnectResult::Denied:
			set_status(text("TwitchConnect.Denied"));
			break;
		case TwitchAccount::ConnectResult::Failed:
			set_status(text("TwitchConnect.Failed").arg(detail));
			break;
		case TwitchAccount::ConnectResult::Cancelled:
			return;
		}
		offer_retry();
	}

	std::shared_ptr<TwitchAccount> account_;
	std::unique_ptr<CancelToken> cancel_; // one per flow, replaced only after a join
	QLabel *code_ = nullptr;
	QLabel *status_ = nullptr;
	QPushButton *copy_ = nullptr;
	QPushButton *open_ = nullptr;
	QPushButton *retry_ = nullptr;
	QPushButton *close_ = nullptr; // "Cancel" while waiting, "Close" after a failure
	QString verification_uri_;
	std::thread worker_; // joined in the destructor
};

} // namespace

bool run_twitch_connect_dialog(QWidget *parent, std::shared_ptr<TwitchAccount> account)
{
	TwitchConnectDialog dialog(parent, std::move(account));
	return dialog.exec() == QDialog::Accepted;
}

} // namespace goalbar::ui

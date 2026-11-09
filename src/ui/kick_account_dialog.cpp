#include "ui/kick_account_dialog.hpp"

#include "core/kick_api.hpp"

#include <obs-module.h>

#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
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

class KickAccountDialog final : public QDialog {
public:
	KickAccountDialog(QWidget *parent, std::shared_ptr<KickAccount> account)
		: QDialog(parent),
		  account_(std::move(account))
	{
		setWindowTitle(text("KickAccount.Title"));
		build_layout();
		show_saved_state();
	}

	~KickAccountDialog() override
	{
		// Aborts the token request in flight; the worker then ends promptly.
		if (cancel_)
			cancel_->cancel();
		if (worker_.joinable())
			worker_.join();
	}

	KickAccountDialog(const KickAccountDialog &) = delete;
	KickAccountDialog &operator=(const KickAccountDialog &) = delete;

	void reject() override
	{
		if (cancel_)
			cancel_->cancel();
		QDialog::reject();
	}

private:
	void build_layout()
	{
		const QString developer_url = QString::fromUtf8(kKickDeveloperUrl.data(), int(kKickDeveloperUrl.size()));
		auto *hint = new QLabel(text("KickAccount.Hint").arg(
						QStringLiteral("<a href=\"%1\">kick.com/settings/developer</a>")
							.arg(developer_url)),
					this);
		hint->setWordWrap(true);
		hint->setTextFormat(Qt::RichText);
		hint->setOpenExternalLinks(true);

		client_id_ = new QLineEdit(this);
		client_secret_ = new QLineEdit(this);
		client_secret_->setEchoMode(QLineEdit::Password);

		auto *form = new QFormLayout();
		form->addRow(text("KickAccount.ClientId"), client_id_);
		form->addRow(text("KickAccount.ClientSecret"), client_secret_);

		status_ = new QLabel(this);
		status_->setWordWrap(true);

		save_ = new QPushButton(text("KickAccount.SaveAndTest"), this);
		remove_ = new QPushButton(text("KickAccount.Remove"), this);
		auto *close = new QPushButton(text("KickAccount.Close"), this);
		save_->setDefault(true);
		connect(save_, &QPushButton::clicked, this, [this] { save_and_test(); });
		connect(remove_, &QPushButton::clicked, this, [this] { remove(); });
		connect(close, &QPushButton::clicked, this, &KickAccountDialog::reject);

		auto *buttons = new QHBoxLayout();
		buttons->addWidget(remove_);
		buttons->addStretch();
		buttons->addWidget(save_);
		buttons->addWidget(close);

		auto *layout = new QVBoxLayout(this);
		layout->addWidget(hint);
		layout->addLayout(form);
		layout->addWidget(status_);
		layout->addLayout(buttons);
		setMinimumWidth(440);
	}

	// Shows the stored client ID and whether a secret is saved; the secret
	// itself never comes back into the UI.
	void show_saved_state()
	{
		const KickAccount::Status status = account_->status();
		client_id_->setText(QString::fromStdString(status.client_id));
		client_secret_->clear();
		client_secret_->setPlaceholderText(status.configured ? text("KickAccount.SecretSaved") : QString());
		remove_->setEnabled(status.configured || !status.client_id.empty());
		if (status.rejected)
			status_->setText(text("KickAccount.Rejected"));
		else if (status.configured)
			status_->setText(text("KickAccount.Saved"));
		else
			status_->setText(text("KickAccount.NotSetUp"));
	}

	// Runs `fn` on the UI thread; dropped if the dialog is gone by then.
	template<typename Fn> void post(Fn fn) { QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection); }

	void set_busy(bool busy)
	{
		save_->setEnabled(!busy);
		remove_->setEnabled(!busy);
		client_id_->setEnabled(!busy);
		client_secret_->setEnabled(!busy);
	}

	void save_and_test()
	{
		const std::string id = client_id_->text().trimmed().toStdString();
		const std::string secret = client_secret_->text().trimmed().toStdString();
		const bool has_saved_secret = account_->status().configured;
		if (id.empty() || (secret.empty() && !has_saved_secret)) {
			status_->setText(text("KickAccount.Missing"));
			return;
		}
		if (const auto error = account_->save_credentials(id, secret)) {
			status_->setText(text("KickAccount.SaveFailed").arg(QString::fromStdString(error->message)));
			return;
		}
		client_secret_->clear();
		client_secret_->setPlaceholderText(text("KickAccount.SecretSaved"));

		// The previous test (if any) posted its result and is returning.
		if (worker_.joinable())
			worker_.join();
		cancel_ = std::make_unique<CancelToken>();
		set_busy(true);
		status_->setText(text("KickAccount.Testing"));
		worker_ = std::thread([this, cancel = cancel_.get()] {
			std::string error;
			const KickAccount::TestResult result = account_->test(*cancel, error);
			if (cancel->cancelled())
				return;
			const QString detail = QString::fromStdString(error);
			post([this, result, detail] { show_result(result, detail); });
		});
	}

	void show_result(KickAccount::TestResult result, const QString &detail)
	{
		set_busy(false);
		switch (result) {
		case KickAccount::TestResult::Ok:
			status_->setText(text("KickAccount.Works"));
			break;
		case KickAccount::TestResult::NotSetUp:
			status_->setText(text("KickAccount.NotSetUp"));
			break;
		case KickAccount::TestResult::BadCredentials:
			status_->setText(text("KickAccount.Rejected"));
			break;
		case KickAccount::TestResult::RateLimited:
			status_->setText(text("KickAccount.RateLimited"));
			break;
		case KickAccount::TestResult::Failed:
			status_->setText(text("KickAccount.Failed").arg(detail));
			break;
		}
	}

	void remove()
	{
		account_->clear_credentials();
		show_saved_state();
	}

	std::shared_ptr<KickAccount> account_;
	std::unique_ptr<CancelToken> cancel_; // one per test, replaced only after a join
	QLineEdit *client_id_ = nullptr;
	QLineEdit *client_secret_ = nullptr;
	QLabel *status_ = nullptr;
	QPushButton *save_ = nullptr;
	QPushButton *remove_ = nullptr;
	std::thread worker_; // joined in the destructor
};

} // namespace

void run_kick_account_dialog(QWidget *parent, std::shared_ptr<KickAccount> account)
{
	KickAccountDialog dialog(parent, std::move(account));
	dialog.exec();
}

} // namespace goalbar::ui

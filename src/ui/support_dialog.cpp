#include "ui/support_dialog.hpp"

#include <obs-module.h>

#include <QDesktopServices>
#include <QDialog>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace goalbar::ui {
namespace {

constexpr const char *kKofiUrl = "https://ko-fi.com/alejoxcode";

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

// Same look as AC Multichat's support message.
void run_support_dialog(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(text("Support.Link"));
	dialog.setMinimumWidth(380);
	auto *layout = new QVBoxLayout(&dialog);
	layout->setContentsMargins(22, 20, 22, 18);
	layout->setSpacing(12);

	auto *title = new QLabel(QStringLiteral("☕  ") + text("Support.Title"), &dialog);
	QFont title_font = title->font();
	title_font.setBold(true);
	title_font.setPointSizeF(title_font.pointSizeF() * 1.3);
	title->setFont(title_font);
	title->setWordWrap(true);
	layout->addWidget(title);

	for (const char *key : {"Support.Free", "Support.Why", "Support.NoPressure"}) {
		auto *paragraph = new QLabel(text(key), &dialog);
		paragraph->setWordWrap(true);
		layout->addWidget(paragraph);
	}
	layout->addSpacing(4);

	auto *kofi = new QPushButton(text("Support.Kofi"), &dialog);
	kofi->setDefault(true);
	kofi->setCursor(Qt::PointingHandCursor);
	kofi->setMinimumHeight(34);
	kofi->setStyleSheet("QPushButton { background-color: palette(highlight); color: palette(highlighted-text); "
			    "font-weight: bold; border: none; border-radius: 6px; padding: 6px 12px; }");
	auto *keep_free = new QPushButton(text("Support.Continue"), &dialog);
	keep_free->setMinimumHeight(34);
	layout->addWidget(kofi);
	layout->addWidget(keep_free);

	QObject::connect(kofi, &QPushButton::clicked, &dialog, [&dialog] {
		QDesktopServices::openUrl(QUrl(QString::fromLatin1(kKofiUrl)));
		dialog.accept();
	});
	QObject::connect(keep_free, &QPushButton::clicked, &dialog, &QDialog::reject);
	dialog.exec();
}

} // namespace goalbar::ui

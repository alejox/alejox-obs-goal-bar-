#include "translator.hpp"

#include "ui/ui_text.hpp"

#include <QFile>
#include <QLocale>

namespace goalbar::app {

bool parse_locale_line(const std::string &line, std::string &key, std::string &value)
{
	const std::size_t eq = line.find('=');
	if (eq == std::string::npos || eq == 0 || line[0] == '#' || line[0] == ';')
		return false;
	key = line.substr(0, eq);
	std::string raw = line.substr(eq + 1);
	while (!raw.empty() && (raw.back() == '\r' || raw.back() == ' '))
		raw.pop_back();
	if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
		raw = raw.substr(1, raw.size() - 2);
	value.clear();
	for (std::size_t i = 0; i < raw.size(); ++i) {
		if (raw[i] == '\\' && i + 1 < raw.size()) {
			const char next = raw[++i];
			value += next == 'n' ? '\n' : next;
		} else {
			value += raw[i];
		}
	}
	return true;
}

Translator &Translator::instance()
{
	static Translator translator;
	return translator;
}

void Translator::read(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return;
	while (!file.atEnd()) {
		const std::string line = file.readLine().toStdString();
		std::string key, value;
		if (parse_locale_line(line.substr(0, line.find_last_not_of("\r\n") + 1), key, value))
			texts_[key] = value;
	}
}

void Translator::load(const QString &data_dir, const std::string &language)
{
	spanish_ = language == "es" || (language.empty() && QLocale::system().language() == QLocale::Spanish);
	texts_.clear();
	// English first: a key missing in Spanish still shows in English.
	read(data_dir + "/locale/en-US.ini");
	read(data_dir + "/app-locale/en-US.ini");
	if (spanish_) {
		read(data_dir + "/locale/es-ES.ini");
		read(data_dir + "/app-locale/es-ES.ini");
	}
}

QString Translator::text(const char *key) const
{
	const auto it = texts_.find(key);
	return it == texts_.end() ? QString::fromUtf8(key) : QString::fromStdString(it->second);
}

std::string Translator::utf8(const std::string &key) const
{
	const auto it = texts_.find(key);
	return it == texts_.end() ? key : it->second;
}

} // namespace goalbar::app

namespace goalbar::ui {

QString ui_text(const char *key)
{
	return app::Translator::instance().text(key);
}

} // namespace goalbar::ui

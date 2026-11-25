// Overlay app: the UI text, read from the plugin's locale files plus the
// app's own (key="value" lines, as OBS reads them). Spanish when the system or
// the user picks it, English otherwise.
#pragma once

#include <QString>

#include <map>
#include <string>

namespace goalbar::app {

class Translator {
public:
	// `language` is "es" or "en"; "" follows the system.
	void load(const QString &data_dir, const std::string &language);
	QString text(const char *key) const;
	std::string utf8(const std::string &key) const;
	bool spanish() const { return spanish_; }

	static Translator &instance();

private:
	void read(const QString &path);

	std::map<std::string, std::string> texts_;
	bool spanish_ = false;
};

inline QString tr_text(const char *key)
{
	return Translator::instance().text(key);
}

// Parses one key="value" line; false for comments and blank lines.
bool parse_locale_line(const std::string &line, std::string &key, std::string &value);

} // namespace goalbar::app

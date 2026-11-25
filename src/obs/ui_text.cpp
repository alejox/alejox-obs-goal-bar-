#include "ui/ui_text.hpp"

#include <obs-module.h>

namespace goalbar::ui {

QString ui_text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace goalbar::ui

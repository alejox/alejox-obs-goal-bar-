// Qt UI: "Is it useful? Buy me a coffee", a short message with the Ko-fi
// link. It opens only when the user clicks the button at the end of the
// source properties; it never shows on its own and stores nothing.
#pragma once

class QWidget;

namespace goalbar::ui {

// Modal. UI thread only.
void run_support_dialog(QWidget *parent);

} // namespace goalbar::ui

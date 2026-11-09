// Qt UI: "Connect Twitch" dialog. Shows the Device Code Grant Flow user code
// and waits for the approval on twitch.tv/activate. The Twitch requests run on
// a background thread; the dialog only lives on the UI thread.
#pragma once

#include "providers/twitch_account.hpp"

#include <memory>

class QWidget;

namespace goalbar::ui {

// Modal. Returns true when the account got connected. UI thread only.
bool run_twitch_connect_dialog(QWidget *parent, std::shared_ptr<TwitchAccount> account);

} // namespace goalbar::ui

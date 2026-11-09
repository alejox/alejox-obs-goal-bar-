// Qt UI: "Kick account" dialog. The user pastes the client ID and secret of
// their own Kick app; they go to the OS secret store, never to the scene
// settings. "Save and test" requests an app token on a background thread.
#pragma once

#include "providers/kick_account.hpp"

#include <memory>

class QWidget;

namespace goalbar::ui {

// Modal. UI thread only.
void run_kick_account_dialog(QWidget *parent, std::shared_ptr<KickAccount> account);

} // namespace goalbar::ui

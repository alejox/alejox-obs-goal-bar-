// System adapter: the process-wide Twitch account, wired to the system network
// stack, the OS secret store and the log sink.
#pragma once

#include "providers/twitch_account.hpp"

#include <memory>

namespace goalbar::sys {

// Created on first use and shared by every goal bar and the UI.
std::shared_ptr<TwitchAccount> twitch_account();

} // namespace goalbar::sys

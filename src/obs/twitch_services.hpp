// OBS adapter: the process-wide Twitch account, wired to the system network
// stack, the OS secret store and the OBS log.
#pragma once

#include "providers/twitch_account.hpp"

#include <memory>

namespace goalbar::obs {

// Created on first use and shared by every Goal Bar source and the UI.
std::shared_ptr<TwitchAccount> twitch_account();

} // namespace goalbar::obs

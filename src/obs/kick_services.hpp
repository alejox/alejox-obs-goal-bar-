// OBS adapter: the process-wide Kick app account, wired to the system network
// stack, the OS secret store and the OBS log.
#pragma once

#include "providers/kick_account.hpp"

#include <memory>

namespace goalbar::obs {

// Created on first use and shared by every Goal Bar source and the UI.
std::shared_ptr<KickAccount> kick_account();

} // namespace goalbar::obs

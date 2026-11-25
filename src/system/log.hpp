// System adapters (network, secret store, providers) log through this hook so
// that they need neither OBS nor Qt: the plugin sends the lines to the OBS
// log, the overlay app to its own log.
#pragma once

#include <functional>
#include <string>

namespace goalbar::sys {

using LogSink = std::function<void(const std::string &message)>;

// Set once at startup, before any provider or account exists.
void set_log_sink(LogSink sink);
void log(const std::string &message);

} // namespace goalbar::sys

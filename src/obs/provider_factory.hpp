// OBS adapter: builds metric providers wired to the system network stack and
// the OBS log.
#pragma once

#include "core/metric_provider.hpp"

#include <memory>
#include <string>

namespace goalbar::obs {

// A YouTube public-mode provider for a canonical channel URL (see
// normalize_youtube_channel). It starts polling at once.
std::unique_ptr<MetricProvider> make_youtube_provider(const std::string &channel_url, Metric metric);

} // namespace goalbar::obs

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

// A Twitch provider for Followers or Subscribers on the shared account. It
// starts polling at once and waits quietly while no account is connected.
std::unique_ptr<MetricProvider> make_twitch_provider(Metric metric);

// A Kick provider for Followers or Viewers of a normalized channel slug, read
// from Kick's public channel endpoint (no credentials). It starts polling at
// once.
std::unique_ptr<MetricProvider> make_kick_provider(const std::string &slug, Metric metric);

// A provider that counts bits (Twitch) or comments (Twitch, YouTube, Kick)
// from the live chat while it runs, starting at zero. `channel` is the
// canonical YouTube URL or the Kick slug; Twitch reads the connected
// account's own chat. A comment needs at least `min_chars` characters.
std::unique_ptr<MetricProvider> make_chat_provider(Platform platform, Metric metric, int min_chars,
						   const std::string &channel);

} // namespace goalbar::obs

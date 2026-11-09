// Pure core: no OBS, no Qt, no network.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace goalbar {

// What one fetch of a channel's public `/live` page tells us.
struct YoutubeLiveSnapshot {
	bool live = false;                     // a broadcast is on air right now
	std::optional<std::string> video_id;   // set only when live
	std::optional<uint64_t> likes;         // exact; set only when live and not hidden
	std::optional<uint64_t> subscribers;   // rounded by YouTube (e.g. 15.8M)
};

// Canonical channel URL for "@handle", a bare handle, a channel id ("UC..."),
// "/channel/UC...", "/c/name", "/user/name" or any youtube.com URL of those.
// Trailing tabs ("/live", "/videos"), queries and whitespace are dropped and the
// handle case is kept. Returns nullopt for empty input, other hosts and videos.
std::optional<std::string> normalize_youtube_channel(std::string input);

// The page to poll for a canonical channel URL, forcing English text.
std::string youtube_live_url(const std::string &channel_url);

// Parses an English subscriber text: "15.8 million subscribers", "12.3K
// subscribers", "1,234 subscribers", "1 subscriber". Nullopt when unreadable.
std::optional<uint64_t> parse_subscriber_text(std::string_view text);

// Reads the live state and counters from the HTML of a `/live` page. Live means
// the page is a watch page (canonical watch URL) with `"isLiveNow":true`, the
// same rule multi-chat uses; upcoming and ended streams are not live.
YoutubeLiveSnapshot parse_youtube_live_page(std::string_view html);

} // namespace goalbar

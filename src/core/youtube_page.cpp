#include "core/youtube_page.hpp"

#include <array>
#include <cctype>
#include <limits>
#include <vector>

namespace goalbar {
namespace {

constexpr std::string_view kBaseUrl = "https://www.youtube.com/";
// How far past a marker the value it labels may sit.
constexpr size_t kMarkerWindow = 400;

bool is_space(char c)
{
	return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

char lower(char c)
{
	return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool starts_with_nocase(std::string_view text, std::string_view prefix)
{
	if (text.size() < prefix.size())
		return false;
	for (size_t i = 0; i < prefix.size(); ++i)
		if (lower(text[i]) != lower(prefix[i]))
			return false;
	return true;
}

std::string_view trim(std::string_view text)
{
	while (!text.empty() && is_space(text.front()))
		text.remove_prefix(1);
	while (!text.empty() && is_space(text.back()))
		text.remove_suffix(1);
	return text;
}

std::vector<std::string_view> split_path(std::string_view path)
{
	std::vector<std::string_view> parts;
	while (!path.empty()) {
		const size_t slash = path.find('/');
		const std::string_view part = path.substr(0, slash);
		if (!part.empty())
			parts.push_back(part);
		if (slash == std::string_view::npos)
			break;
		path.remove_prefix(slash + 1);
	}
	return parts;
}

// A path segment usable in a channel URL: no whitespace and no URL syntax.
bool is_name(std::string_view name)
{
	if (name.empty())
		return false;
	for (char c : name)
		if (is_space(c) || c == '/' || c == '?' || c == '#' || c == '"' || c == '&' || c == '=' || c == '@')
			return false;
	return true;
}

bool is_channel_id(std::string_view text)
{
	return text.size() == 24 && text.substr(0, 2) == "UC" && is_name(text);
}

std::optional<std::string> channel_from_segments(const std::vector<std::string_view> &parts)
{
	if (parts.empty())
		return std::nullopt;
	const std::string_view first = parts[0];
	if (first.size() > 1 && first.front() == '@' && is_name(first.substr(1)))
		return std::string(kBaseUrl) + std::string(first);
	if (first == "channel")
		return parts.size() > 1 && is_channel_id(parts[1])
			       ? std::optional<std::string>(std::string(kBaseUrl) + "channel/" + std::string(parts[1]))
			       : std::nullopt;
	if (first == "c" || first == "user")
		return parts.size() > 1 && is_name(parts[1]) ? std::optional<std::string>(std::string(kBaseUrl) +
											   std::string(first) + "/" +
											   std::string(parts[1]))
							     : std::nullopt;
	return std::nullopt;
}

// Removes "http(s)://", "www."/"m." and "youtube.com". Returns false when a
// different host is named.
bool strip_host(std::string_view &text, bool &had_host)
{
	for (std::string_view scheme : {"https://", "http://"})
		if (starts_with_nocase(text, scheme)) {
			text.remove_prefix(scheme.size());
			had_host = true;
		}
	for (std::string_view sub : {"www.", "m."})
		if (starts_with_nocase(text, sub)) {
			text.remove_prefix(sub.size());
			had_host = true;
		}
	if (starts_with_nocase(text, "youtube.com")) {
		text.remove_prefix(std::string_view("youtube.com").size());
		return text.empty() || text.front() == '/';
	}
	return !had_host;
}

// Digits of `text` up to the first character that is neither a digit nor a
// thousands separator; nullopt when there are none or the value overflows.
std::optional<uint64_t> leading_count(std::string_view text)
{
	uint64_t value = 0;
	bool any = false;
	for (char c : text) {
		if (c == ',')
			continue;
		if (!is_digit(c))
			break;
		if (value > (std::numeric_limits<uint64_t>::max() - 9) / 10)
			return std::nullopt;
		value = value * 10 + static_cast<uint64_t>(c - '0');
		any = true;
	}
	return any ? std::optional<uint64_t>(value) : std::nullopt;
}

// The JSON string value right after `marker` (which ends with the opening quote).
std::optional<std::string_view> string_after(std::string_view html, size_t at, std::string_view marker)
{
	if (at == std::string_view::npos || at + marker.size() > html.size())
		return std::nullopt;
	const size_t start = at + marker.size();
	const size_t end = html.find('"', start);
	if (end == std::string_view::npos)
		return std::nullopt;
	return html.substr(start, end - start);
}

std::optional<std::string> canonical_video_id(std::string_view html)
{
	constexpr std::string_view marker = R"(<link rel="canonical" href="https://www.youtube.com/watch?v=)";
	const auto id = string_after(html, html.find(marker), marker);
	if (!id || id->size() != 11)
		return std::nullopt;
	for (char c : *id)
		if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
			return std::nullopt;
	return std::string(*id);
}

std::optional<uint64_t> parse_likes(std::string_view html)
{
	constexpr std::string_view exact = R"("likeCount":")";
	const auto digits = string_after(html, html.find(exact), exact);
	if (digits && digits->find_first_not_of("0123456789") == std::string_view::npos)
		if (const auto count = leading_count(*digits))
			return count;

	// "like this video along with 5,150 other people" (the viewer's own like is excluded).
	constexpr std::string_view along = "along with ";
	for (size_t at = html.find(along); at != std::string_view::npos; at = html.find(along, at + 1)) {
		const std::string_view rest = html.substr(at + along.size(), 40);
		const size_t end = rest.find(" other people");
		if (end != std::string_view::npos)
			if (const auto count = leading_count(rest.substr(0, end)))
				return count;
	}
	return std::nullopt;
}

std::optional<uint64_t> parse_subscribers(std::string_view html)
{
	const size_t block = html.find(R"("subscriberCountText":)");
	if (block != std::string_view::npos) {
		const std::string_view window = html.substr(block, kMarkerWindow);
		for (std::string_view marker : {R"("label":")", R"("simpleText":")"})
			if (const auto text = string_after(window, window.find(marker), marker))
				if (const auto count = parse_subscriber_text(*text))
					return count;
	}
	// Channel header (new layout): {"content":"12.3K subscribers"}.
	constexpr std::string_view content = R"("content":")";
	for (size_t at = html.find(content); at != std::string_view::npos; at = html.find(content, at + 1)) {
		const auto text = string_after(html, at, content);
		if (text && text->size() < 64 && text->find("subscriber") != std::string_view::npos)
			if (const auto count = parse_subscriber_text(*text))
				return count;
	}
	return std::nullopt;
}

} // namespace

std::optional<std::string> normalize_youtube_channel(std::string input)
{
	std::string_view text = trim(input);
	if (text.empty())
		return std::nullopt;

	bool had_host = false;
	if (!strip_host(text, had_host))
		return std::nullopt;
	text = text.substr(0, text.find_first_of("?#"));

	const std::vector<std::string_view> parts = split_path(text);
	if (const auto channel = channel_from_segments(parts))
		return channel;
	if (had_host || parts.size() != 1 || text.front() == '/')
		return std::nullopt;

	// A bare word: a channel id or a handle typed without "@".
	if (is_channel_id(parts[0]))
		return std::string(kBaseUrl) + "channel/" + std::string(parts[0]);
	return is_name(parts[0]) ? std::optional<std::string>(std::string(kBaseUrl) + "@" + std::string(parts[0]))
				 : std::nullopt;
}

std::string youtube_live_url(const std::string &channel_url)
{
	return channel_url + "/live?hl=en";
}

std::optional<uint64_t> parse_subscriber_text(std::string_view text)
{
	text = trim(text);
	if (text.empty() || !is_digit(text.front()))
		return std::nullopt;

	// Integer part (with thousands separators), then up to 3 decimals.
	size_t i = 0;
	uint64_t whole = 0;
	for (; i < text.size() && (is_digit(text[i]) || text[i] == ','); ++i)
		if (text[i] != ',') {
			if (whole > std::numeric_limits<uint64_t>::max() / 10 - 9)
				return std::nullopt;
			whole = whole * 10 + static_cast<uint64_t>(text[i] - '0');
		}
	uint64_t fraction = 0; // in thousandths
	if (i < text.size() && text[i] == '.') {
		uint64_t scale = 100;
		for (++i; i < text.size() && is_digit(text[i]); ++i) {
			fraction += static_cast<uint64_t>(text[i] - '0') * scale;
			scale /= 10;
		}
	}
	std::string_view rest = trim(text.substr(i));

	struct Unit {
		std::string_view name;
		uint64_t factor;
	};
	constexpr std::array<Unit, 6> units{{{"thousand", 1000},
					     {"million", 1000000},
					     {"billion", 1000000000},
					     {"k", 1000},
					     {"m", 1000000},
					     {"b", 1000000000}}};
	uint64_t factor = 1;
	for (const Unit &unit : units)
		if (starts_with_nocase(rest, unit.name)) {
			factor = unit.factor;
			rest = trim(rest.substr(unit.name.size()));
			break;
		}
	if (!starts_with_nocase(rest, "subscriber"))
		return std::nullopt;
	if (factor == 1)
		return fraction == 0 ? std::optional<uint64_t>(whole) : std::nullopt;
	if (whole > std::numeric_limits<uint64_t>::max() / factor)
		return std::nullopt;
	return whole * factor + fraction * (factor / 1000);
}

YoutubeLiveSnapshot parse_youtube_live_page(std::string_view html)
{
	YoutubeLiveSnapshot snap;
	snap.subscribers = parse_subscribers(html);
	const auto video = canonical_video_id(html);
	if (!video || html.find(R"("isLiveNow":true)") == std::string_view::npos)
		return snap;
	snap.live = true;
	snap.video_id = video;
	snap.likes = parse_likes(html);
	return snap;
}

} // namespace goalbar

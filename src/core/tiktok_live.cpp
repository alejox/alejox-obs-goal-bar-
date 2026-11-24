#include "core/tiktok_live.hpp"

#include "core/twitch_auth.hpp" // url_encode

#include <json.hpp>

#include <algorithm>
#include <cctype>

namespace goalbar {
namespace {

using nlohmann::json;

std::string lowercase(std::string_view text)
{
	std::string out(text);
	std::transform(out.begin(), out.end(), out.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

void strip_prefix(std::string_view &text, std::string_view prefix)
{
	if (text.substr(0, prefix.size()) == prefix)
		text.remove_prefix(prefix.size());
}

bool is_handle_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
}

// A non-negative count sent as a number or as a string of digits.
std::optional<uint64_t> count_of(const json &value)
{
	if (value.is_number_unsigned())
		return value.get<uint64_t>();
	if (value.is_number_integer())
		return value.get<long long>() < 0 ? std::nullopt : std::optional<uint64_t>(value.get<long long>());
	if (!value.is_string())
		return std::nullopt;
	const std::string &text = value.get_ref<const std::string &>();
	if (text.empty() || text.size() > 19)
		return std::nullopt;
	uint64_t out = 0;
	for (char c : text) {
		if (c < '0' || c > '9')
			return std::nullopt;
		out = out * 10 + uint64_t(c - '0');
	}
	return out;
}

const json *member(const json &object, const char *key)
{
	if (!object.is_object())
		return nullptr;
	const auto it = object.find(key);
	return it == object.end() ? nullptr : &*it;
}

std::string push_frame(std::string_view type, std::string_view payload, uint64_t log_id = 0)
{
	ProtoWriter w;
	if (log_id)
		w.varint(2, log_id);
	w.bytes(6, "pb").bytes(7, type).bytes(8, payload);
	return w.data();
}

ChatEvent comment_event(std::string_view payload)
{
	ChatEvent event;
	event.is_message = true;
	ProtoReader p(payload);
	while (p.next())
		if (p.field() == 3) // content
			event.length = chat_text_length(p.bytes());
	return event;
}

std::optional<ChatEvent> gift_event(std::string_view payload)
{
	uint64_t repeat = 1, repeat_end = 0, type = 0, diamonds = 0;
	ProtoReader p(payload);
	while (p.next()) {
		if (p.field() == 5) {
			repeat = p.varint();
		} else if (p.field() == 9) {
			repeat_end = p.varint();
		} else if (p.field() == 15) { // the gift
			ProtoReader g(p.bytes());
			while (g.next()) {
				if (g.field() == 11)
					type = g.varint();
				else if (g.field() == 12)
					diamonds = g.varint();
			}
		}
	}
	// A streak gift (type 1) arrives once per tap with the running count;
	// only the last one (repeat_end) has the final count.
	if (type == 1 && !repeat_end)
		return std::nullopt;
	if (diamonds == 0)
		return std::nullopt;
	ChatEvent event;
	event.diamonds = diamonds * std::max<uint64_t>(repeat, 1);
	return event;
}

std::optional<ChatEvent> like_event(std::string_view payload)
{
	ProtoReader p(payload);
	while (p.next()) {
		if (p.field() == 3 && p.varint() > 0) { // the room's total
			ChatEvent event;
			event.room_likes = p.varint();
			return event;
		}
	}
	return std::nullopt;
}

// action 3: the live ended; 4: TikTok suspended it.
bool stream_ended(std::string_view payload)
{
	ProtoReader p(payload);
	while (p.next())
		if (p.field() == 2)
			return p.varint() == 3 || p.varint() == 4;
	return false;
}

} // namespace

std::optional<std::string> normalize_tiktok_handle(std::string_view input)
{
	while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front())))
		input.remove_prefix(1);
	while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())))
		input.remove_suffix(1);
	const std::string lowered = lowercase(input);
	std::string_view text = lowered;
	strip_prefix(text, "https://");
	strip_prefix(text, "http://");
	strip_prefix(text, "www.");
	strip_prefix(text, "m.");
	strip_prefix(text, "tiktok.com/");
	strip_prefix(text, "@");
	if (const auto end = text.find_first_of("/?#"); end != std::string_view::npos)
		text = text.substr(0, end); // "name/live", "name?lang=es"
	if (text.empty() || text.size() > kTikTokHandleMaxLength)
		return std::nullopt;
	if (!std::all_of(text.begin(), text.end(), is_handle_char))
		return std::nullopt;
	return std::string(text);
}

std::string tiktok_room_url(std::string_view handle)
{
	return "https://www.tiktok.com/api-live/user/room/?aid=1988&app_name=tiktok_web&device_platform=web_pc"
	       "&sourceType=54&uniqueId=" +
	       url_encode(handle);
}

std::vector<std::pair<std::string, std::string>> tiktok_room_headers()
{
	return {{"Accept", "application/json"}, {"Referer", "https://www.tiktok.com/"}};
}

TikTokRoomLookup parse_tiktok_room(int status, std::string_view body)
{
	TikTokRoomLookup lookup;
	lookup.http_status = status;
	if (status == 429) {
		lookup.status = TikTokRoomStatus::RateLimited;
		return lookup;
	}
	const json parsed = json::parse(body.begin(), body.end(), nullptr, false);
	const bool ok = status >= 200 && status < 300;
	// TikTok answers a client it does not trust with an HTML page or nothing.
	if (status == 403 || (ok && (parsed.is_discarded() || !parsed.is_object()))) {
		lookup.status = TikTokRoomStatus::Blocked;
		return lookup;
	}
	if (!ok) {
		lookup.status = TikTokRoomStatus::HttpError;
		return lookup;
	}
	if (const json *message = member(parsed, "message");
	    message && message->is_string() && message->get<std::string>() == "user_not_found") {
		lookup.status = TikTokRoomStatus::NotFound;
		return lookup;
	}
	const json *data = member(parsed, "data");
	if (!data || !data->is_object())
		return lookup; // Malformed

	TikTokRoom room;
	if (const json *user = member(*data, "user")) {
		if (const json *id = member(*user, "roomId")) {
			if (id->is_string())
				room.room_id = id->get<std::string>();
			else if (const auto n = count_of(*id))
				room.room_id = std::to_string(*n);
		}
	}
	if (const json *stats = member(*data, "stats"))
		if (const json *followers = member(*stats, "followerCount"))
			room.followers = count_of(*followers);
	if (const json *live = member(*data, "liveRoom")) {
		const json *state = member(*live, "status");
		room.is_live = state && count_of(*state) == uint64_t{2};
		if (room.is_live)
			if (const json *stats = member(*live, "liveRoomStats"))
				if (const json *users = member(*stats, "userCount"))
					room.viewers = count_of(*users);
	}
	if (room.room_id == "0")
		room.room_id.clear();
	lookup.status = TikTokRoomStatus::Found;
	lookup.room = room;
	return lookup;
}

MetricReading tiktok_metric_reading(const TikTokRoomLookup &lookup, Metric metric)
{
	MetricReading reading;
	reading.state = ProviderState::Error;
	switch (lookup.status) {
	case TikTokRoomStatus::Found:
		break;
	case TikTokRoomStatus::NotFound:
		reading.message = kTikTokUserNotFound;
		return reading;
	case TikTokRoomStatus::Blocked:
		reading.message = kTikTokBlocked;
		return reading;
	case TikTokRoomStatus::RateLimited:
		reading.state = ProviderState::RateLimited;
		reading.message = "TikTok rate limit";
		return reading;
	case TikTokRoomStatus::HttpError:
		reading.message = "HTTP " + std::to_string(lookup.http_status);
		return reading;
	case TikTokRoomStatus::Malformed:
		reading.message = "unexpected answer from TikTok";
		return reading;
	}
	const TikTokRoom &room = *lookup.room;
	if (metric == Metric::Followers) {
		if (!room.followers) {
			reading.message = "follower count missing in the TikTok answer";
			return reading;
		}
		reading.state = ProviderState::Live;
		reading.value = room.followers;
		return reading;
	}
	if (metric != Metric::Viewers) {
		reading.message = "TikTok's room does not publish this metric";
		return reading;
	}
	if (!room.is_live) {
		reading.state = ProviderState::NotLive;
		return reading;
	}
	if (!room.viewers) {
		reading.message = "viewer count missing in the TikTok answer";
		return reading;
	}
	reading.state = ProviderState::Live;
	reading.value = room.viewers;
	return reading;
}

// ---- protobuf ---------------------------------------------------------------

bool ProtoReader::read_varint(uint64_t &out)
{
	out = 0;
	for (int shift = 0; shift < 64; shift += 7) {
		if (pos_ >= data_.size())
			return false;
		const auto b = static_cast<unsigned char>(data_[pos_++]);
		out |= uint64_t(b & 0x7f) << shift;
		if (!(b & 0x80))
			return true;
	}
	return false;
}

bool ProtoReader::next()
{
	uint64_t key = 0;
	if (pos_ >= data_.size() || !read_varint(key))
		return false;
	field_ = int(key >> 3);
	varint_ = 0;
	bytes_ = {};
	switch (key & 7) {
	case 0:
		return read_varint(varint_);
	case 1:
		if (data_.size() - pos_ < 8)
			return false;
		pos_ += 8;
		return true;
	case 2: {
		uint64_t length = 0;
		if (!read_varint(length) || length > data_.size() - pos_)
			return false;
		bytes_ = data_.substr(pos_, std::size_t(length));
		pos_ += std::size_t(length);
		return true;
	}
	case 5:
		if (data_.size() - pos_ < 4)
			return false;
		pos_ += 4;
		return true;
	default:
		return false;
	}
}

void ProtoWriter::raw(uint64_t value)
{
	while (value >= 0x80) {
		out_.push_back(char((value & 0x7f) | 0x80));
		value >>= 7;
	}
	out_.push_back(char(value));
}

ProtoWriter &ProtoWriter::varint(int field, uint64_t value)
{
	raw(uint64_t(field) << 3);
	raw(value);
	return *this;
}

ProtoWriter &ProtoWriter::bytes(int field, std::string_view value)
{
	raw((uint64_t(field) << 3) | 2);
	raw(value.size());
	out_.append(value.data(), value.size());
	return *this;
}

// ---- live chat --------------------------------------------------------------

std::string tiktok_sign_url(std::string_view room_id, std::string_view user_agent)
{
	return std::string(kEulerSignServer) + "/webcast/rooms/" + url_encode(room_id) +
	       "/connect?client=obs-goal-bar&client_enter=true&platform=web&user_agent=" + url_encode(user_agent);
}

TikTokFetch parse_tiktok_fetch(std::string_view data)
{
	TikTokFetch fetch;
	ProtoReader p(data);
	while (p.next()) {
		switch (p.field()) {
		case 1: {
			TikTokEvent event;
			ProtoReader m(p.bytes());
			while (m.next()) {
				if (m.field() == 1)
					event.method = std::string(m.bytes());
				else if (m.field() == 2)
					event.payload = std::string(m.bytes());
			}
			fetch.events.push_back(std::move(event));
			break;
		}
		case 5:
			fetch.internal_ext = std::string(p.bytes());
			break;
		case 7: {
			std::string key, value;
			ProtoReader m(p.bytes());
			while (m.next()) {
				if (m.field() == 1)
					key = std::string(m.bytes());
				else if (m.field() == 2)
					value = std::string(m.bytes());
			}
			fetch.route_params.emplace_back(std::move(key), std::move(value));
			break;
		}
		case 9:
			fetch.need_ack = p.varint() != 0;
			break;
		case 10:
			fetch.push_server = std::string(p.bytes());
			break;
		}
	}
	return fetch;
}

std::string tiktok_socket_url(const TikTokFetch &signed_fetch, std::string_view room_id)
{
	std::string query;
	const auto add = [&query](std::string_view part) {
		if (!query.empty())
			query += '&';
		query += part;
	};
	for (const auto &[key, value] : signed_fetch.route_params)
		if (!value.empty())
			add(key + "=" + url_encode(value));
	for (const char *part :
	     {"aid=1988", "app_name=tiktok_web", "browser_platform=Win32", "browser_name=Mozilla", "browser_online=true",
	      "cookie_enabled=true", "device_platform=web", "identity=audience", "live_id=12", "sup_ws_ds_opt=1",
	      "update_version_code=2.0.0", "version_code=180800", "client_enter=1", "ws_direct=1", "did_rule=3",
	      "heartbeat_duration=10000", "resp_content_type=protobuf", "history_comment_count=0", "compress="})
		add(part);
	add("room_id=" + url_encode(room_id));
	add("version_code=270000");
	return signed_fetch.push_server + "?" + query;
}

std::string tiktok_cookie_header(std::string_view set_cookie)
{
	std::string out;
	const auto add = [&out](std::string_view part) {
		if (!out.empty())
			out += "; ";
		out += part;
	};
	if (set_cookie.find("tt-target-idc=") == std::string_view::npos)
		add("tt-target-idc=useast1a");
	while (!set_cookie.empty()) {
		const std::size_t end = set_cookie.find_first_of(";,");
		std::string_view part = set_cookie.substr(0, end);
		set_cookie = end == std::string_view::npos ? std::string_view() : set_cookie.substr(end + 1);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
			part.remove_prefix(1);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
			part.remove_suffix(1);
		const std::size_t eq = part.find('=');
		if (eq == std::string_view::npos || eq == 0)
			continue;
		std::string name = lowercase(part.substr(0, eq));
		while (!name.empty() && name.back() == ' ')
			name.pop_back();
		if (name == "path" || name == "domain" || name == "expires" || name == "max-age" || name == "samesite")
			continue;
		add(part);
	}
	return out;
}

std::string tiktok_enter_room_frame(uint64_t room_id)
{
	return push_frame("im_enter_room",
			  ProtoWriter().varint(1, room_id).varint(4, 12).bytes(5, "audience").bytes(9, "0").data());
}

std::string tiktok_heartbeat_frame(uint64_t room_id, uint64_t sequence)
{
	return push_frame("hb", ProtoWriter().varint(1, room_id).varint(2, sequence).data());
}

std::string tiktok_ack_frame(const TikTokFetch &fetch, uint64_t log_id)
{
	return push_frame("ack", fetch.internal_ext.empty() ? std::string_view("-") : fetch.internal_ext, log_id);
}

TikTokFrame parse_tiktok_frame(std::string_view data)
{
	TikTokFrame frame;
	ProtoReader p(data);
	while (p.next()) {
		if (p.field() == 2) {
			frame.log_id = p.varint();
		} else if (p.field() == 5) {
			std::string key, value;
			ProtoReader h(p.bytes());
			while (h.next())
				(h.field() == 1 ? key : value) = std::string(h.bytes());
			if (key == "compress_type")
				frame.compress = value;
		} else if (p.field() == 7) {
			frame.type = std::string(p.bytes());
		} else if (p.field() == 8) {
			frame.payload = std::string(p.bytes());
		}
	}
	return frame;
}

TikTokChatEvent tiktok_chat_event(const TikTokEvent &event)
{
	TikTokChatEvent out;
	if (event.method == "WebcastChatMessage")
		out.event = comment_event(event.payload);
	else if (event.method == "WebcastGiftMessage")
		out.event = gift_event(event.payload);
	else if (event.method == "WebcastLikeMessage")
		out.event = like_event(event.payload);
	else if (event.method == "WebcastControlMessage")
		out.stream_ended = stream_ended(event.payload);
	return out;
}

} // namespace goalbar

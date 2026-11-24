// Pure core: no OBS, no Qt. TikTok LIVE, read the way multi-chat reads it
// (src/chat/tiktok.cpp there):
//
// - The room endpoint of tiktok.com (GET, no account) gives the followers,
//   whether the creator is live and the viewers of the live room.
// - The live chat is the WebSocket of TikTok's web player, protobuf over
//   binary frames. TikTok wants that connection signed; Euler Stream
//   (api.eulerstream.com, free, no account, a daily limit) signs it and
//   answers with the push server, its parameters and TikTok's cookies.
//
// None of this is documented by TikTok, so it can change or be blocked at
// any time. These build the requests and read the bytes; the adapters do
// the network.
#pragma once

#include "core/chat_events.hpp"
#include "core/metric_provider.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace goalbar {

inline constexpr std::size_t kTikTokHandleMaxLength = 24;

// Accepts "name", "@name", "tiktok.com/@name" or a full profile or live URL
// and returns the lowercase handle without "@"; nullopt when it is empty, too
// long or has characters a handle cannot have (letters, digits, "_", ".").
std::optional<std::string> normalize_tiktok_handle(std::string_view input);

// ---- room endpoint ----------------------------------------------------------

// GET https://www.tiktok.com/api-live/user/room/?…&uniqueId=<handle>
std::string tiktok_room_url(std::string_view handle);
// Headers it needs (TikTok answers an HTML page without them).
std::vector<std::pair<std::string, std::string>> tiktok_room_headers();

struct TikTokRoom {
	std::string room_id;               // data.user.roomId; "" when it never went live
	std::optional<uint64_t> followers; // data.stats.followerCount
	bool is_live = false;              // data.liveRoom.status == 2
	std::optional<uint64_t> viewers;   // data.liveRoom.liveRoomStats.userCount while live
};

enum class TikTokRoomStatus {
	Found,
	NotFound,    // "message":"user_not_found"
	Blocked,     // an HTML page, an empty body or HTTP 403: TikTok refused the request
	RateLimited, // HTTP 429
	HttpError,   // any other non-2xx answer
	Malformed,   // JSON without a data object
};

struct TikTokRoomLookup {
	TikTokRoomStatus status = TikTokRoomStatus::Malformed;
	int http_status = 0;
	std::optional<TikTokRoom> room; // set when Found
};

// `status` is the HTTP status of an answer that arrived (not 0).
TikTokRoomLookup parse_tiktok_room(int status, std::string_view body);

// MetricReading::message values the UI translates.
inline constexpr const char *kTikTokUserNotFound = "TikTok user not found";
inline constexpr const char *kTikTokBlocked = "TikTok blocked the request";
inline constexpr const char *kTikTokSignLimit = "TikTok sign limit";

// Followers are readable at any time; viewers follow the live status
// (NotLive while offline). Other metrics are an error: they come from the chat.
MetricReading tiktok_metric_reading(const TikTokRoomLookup &lookup, Metric metric);

// ---- protobuf ---------------------------------------------------------------

// Reads one message field by field.
class ProtoReader {
public:
	explicit ProtoReader(std::string_view data) : data_(data) {}
	// Moves to the next field; false at the end or on malformed data.
	bool next();
	int field() const { return field_; }
	uint64_t varint() const { return varint_; }
	std::string_view bytes() const { return bytes_; } // strings and sub-messages

private:
	bool read_varint(uint64_t &out);

	std::string_view data_;
	std::size_t pos_ = 0;
	int field_ = 0;
	uint64_t varint_ = 0;
	std::string_view bytes_;
};

class ProtoWriter {
public:
	ProtoWriter &varint(int field, uint64_t value);
	ProtoWriter &bytes(int field, std::string_view value);
	const std::string &data() const { return out_; }

private:
	void raw(uint64_t value);
	std::string out_;
};

// ---- live chat --------------------------------------------------------------

inline constexpr std::string_view kEulerSignServer = "https://api.eulerstream.com";

// GET <Euler>/webcast/rooms/<room>/connect?…: the signed connection, as a
// TikTokFetch protobuf. `user_agent` must be the one the WebSocket will use.
std::string tiktok_sign_url(std::string_view room_id, std::string_view user_agent);

struct TikTokEvent {
	std::string method;  // "WebcastChatMessage", "WebcastGiftMessage", …
	std::string payload; // the protobuf of that message
};

// What the sign server and each chat frame carry (TikTok's FetchResult).
struct TikTokFetch {
	std::vector<TikTokEvent> events;
	std::string internal_ext;
	std::vector<std::pair<std::string, std::string>> route_params;
	bool need_ack = false;
	std::string push_server; // wss://… (only in the sign server's answer)
};

TikTokFetch parse_tiktok_fetch(std::string_view data);

// The WebSocket URL: the push server, the signed parameters and the ones
// TikTok's web player adds.
std::string tiktok_socket_url(const TikTokFetch &signed_fetch, std::string_view room_id);

// The Cookie header for the WebSocket, from the sign server's X-Set-TT-Cookie
// header ("a=1; Path=/, b=2; Domain=…" → "tt-target-idc=useast1a; a=1; b=2").
std::string tiktok_cookie_header(std::string_view set_cookie);

// Binary messages the client sends.
std::string tiktok_enter_room_frame(uint64_t room_id);
std::string tiktok_heartbeat_frame(uint64_t room_id, uint64_t sequence);
std::string tiktok_ack_frame(const TikTokFetch &fetch, uint64_t log_id);

// One binary frame the server sends (TikTok's PushFrame).
struct TikTokFrame {
	uint64_t log_id = 0;
	std::string type;     // "msg" carries a TikTokFetch
	std::string compress; // compress_type header; "" or "none" is plain
	std::string payload;
};

TikTokFrame parse_tiktok_frame(std::string_view data);

// What one event adds to the goals.
struct TikTokChatEvent {
	std::optional<ChatEvent> event; // a comment, a gift (diamonds) or likes (room total)
	bool stream_ended = false;      // WebcastControlMessage: the live ended or was suspended
};

TikTokChatEvent tiktok_chat_event(const TikTokEvent &event);

} // namespace goalbar

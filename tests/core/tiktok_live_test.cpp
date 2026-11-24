#include "core/tiktok_live.hpp"

#include "test_harness.hpp"

#include <string>

using namespace goalbar;

namespace {

// Trimmed from a real room answer for an offline creator (2026-11-24).
constexpr const char *kOfflineRoom = R"({
  "data": {
    "user": {"id": "6726427646474994694", "nickname": "adin", "uniqueId": "adinross",
             "roomId": "7009584415584144134", "status": 4},
    "stats": {"followingCount": 91, "followerCount": 6168938},
    "liveRoom": {"title": "Late night", "status": 4, "liveRoomStats": {"enterCount": 1, "userCount": 1}}
  },
  "message": "",
  "extra": {},
  "statusCode": 0
})";

// The same shape while live (status 2).
constexpr const char *kLiveRoom = R"({
  "data": {
    "user": {"uniqueId": "creator", "roomId": "7441234567890123456", "status": 2},
    "stats": {"followerCount": 120500},
    "liveRoom": {"status": 2, "liveRoomStats": {"enterCount": 9100, "userCount": 1843}}
  },
  "message": "",
  "statusCode": 0
})";

// Real answer for a handle that does not exist (2026-11-24).
constexpr const char *kNoUser =
	R"({"data":null,"message":"user_not_found","extra":{"id":"20261124062151F1063617E9820E09F9BB"},"statusCode":19881007})";

std::string event_bytes(std::string_view method, std::string_view payload)
{
	return ProtoWriter().bytes(1, method).bytes(2, payload).data();
}

std::string user(std::string_view nickname)
{
	return ProtoWriter().bytes(3, nickname).bytes(38, "handle").data();
}

std::string chat(std::string_view text)
{
	return ProtoWriter().bytes(2, user("Ana")).bytes(3, text).data();
}

std::string gift(uint64_t type, uint64_t diamonds, uint64_t repeat, bool repeat_end)
{
	ProtoWriter details;
	details.varint(11, type).varint(12, diamonds).bytes(16, "Rose");
	ProtoWriter w;
	w.varint(5, repeat).bytes(7, user("Ana"));
	if (repeat_end)
		w.varint(9, 1);
	w.bytes(15, details.data());
	return w.data();
}

std::optional<ChatEvent> event_of(std::string_view method, const std::string &payload)
{
	return tiktok_chat_event(TikTokEvent{std::string(method), payload}).event;
}

} // namespace

TEST_CASE(handles_are_normalized)
{
	CHECK(normalize_tiktok_handle("alejoxcode") == std::string("alejoxcode"));
	CHECK(normalize_tiktok_handle("  @AlejoxCode ") == std::string("alejoxcode"));
	CHECK(normalize_tiktok_handle("tiktok.com/@ale.jox_1") == std::string("ale.jox_1"));
	CHECK(normalize_tiktok_handle("https://www.tiktok.com/@alejoxcode/live") == std::string("alejoxcode"));
	CHECK(normalize_tiktok_handle("https://www.tiktok.com/@alejoxcode?lang=es") == std::string("alejoxcode"));
	CHECK(normalize_tiktok_handle("https://m.tiktok.com/@alejoxcode") == std::string("alejoxcode"));
	CHECK(!normalize_tiktok_handle("").has_value());
	CHECK(!normalize_tiktok_handle("@").has_value());
	CHECK(!normalize_tiktok_handle("bad name").has_value());
	CHECK(!normalize_tiktok_handle("name-with-dash").has_value());
	CHECK(!normalize_tiktok_handle("abcdefghijklmnopqrstuvwxy").has_value()); // 25 characters
}

TEST_CASE(room_url_carries_the_handle)
{
	const std::string url = tiktok_room_url("ale.jox");
	CHECK(url.rfind("https://www.tiktok.com/api-live/user/room/?", 0) == 0);
	CHECK(url.find("&uniqueId=ale.jox") != std::string::npos);
	CHECK(url.find("aid=1988") != std::string::npos);
}

TEST_CASE(offline_room_has_followers_but_no_viewers)
{
	const TikTokRoomLookup lookup = parse_tiktok_room(200, kOfflineRoom);
	CHECK(lookup.status == TikTokRoomStatus::Found);
	CHECK_EQ(lookup.room->room_id, std::string("7009584415584144134"));
	CHECK(lookup.room->followers == uint64_t{6168938});
	CHECK(!lookup.room->is_live);
	CHECK(!lookup.room->viewers.has_value()); // userCount 1 of an ended room is not a reading

	const MetricReading followers = tiktok_metric_reading(lookup, Metric::Followers);
	CHECK(followers.state == ProviderState::Live);
	CHECK(followers.value == uint64_t{6168938});
	const MetricReading viewers = tiktok_metric_reading(lookup, Metric::Viewers);
	CHECK(viewers.state == ProviderState::NotLive);
	CHECK(!viewers.value.has_value());
}

TEST_CASE(live_room_has_viewers)
{
	const TikTokRoomLookup lookup = parse_tiktok_room(200, kLiveRoom);
	CHECK(lookup.status == TikTokRoomStatus::Found);
	CHECK(lookup.room->is_live);
	const MetricReading viewers = tiktok_metric_reading(lookup, Metric::Viewers);
	CHECK(viewers.state == ProviderState::Live);
	CHECK(viewers.value == uint64_t{1843});
	CHECK(tiktok_metric_reading(lookup, Metric::Followers).value == uint64_t{120500});
	// Likes, comments and diamonds come from the chat, not from the room.
	CHECK(tiktok_metric_reading(lookup, Metric::Likes).state == ProviderState::Error);
}

TEST_CASE(room_errors_are_classified)
{
	CHECK(parse_tiktok_room(200, kNoUser).status == TikTokRoomStatus::NotFound);
	CHECK_EQ(tiktok_metric_reading(parse_tiktok_room(200, kNoUser), Metric::Followers).message,
		 std::string(kTikTokUserNotFound));
	// TikTok refuses a client it does not trust with HTML or an empty body.
	CHECK(parse_tiktok_room(200, "<!DOCTYPE html><html></html>").status == TikTokRoomStatus::Blocked);
	CHECK(parse_tiktok_room(200, "").status == TikTokRoomStatus::Blocked);
	CHECK(parse_tiktok_room(403, "").status == TikTokRoomStatus::Blocked);
	CHECK(parse_tiktok_room(429, "").status == TikTokRoomStatus::RateLimited);
	CHECK(tiktok_metric_reading(parse_tiktok_room(429, ""), Metric::Followers).state == ProviderState::RateLimited);
	CHECK(parse_tiktok_room(500, "{}").status == TikTokRoomStatus::HttpError);
	CHECK(parse_tiktok_room(200, R"({"message":"","statusCode":0})").status == TikTokRoomStatus::Malformed);
	CHECK(tiktok_metric_reading(parse_tiktok_room(200, kNoUser), Metric::Viewers).state == ProviderState::Error);
}

TEST_CASE(protobuf_round_trips)
{
	const std::string data =
		ProtoWriter().varint(1, 300).bytes(2, "hola").varint(3, 7441234567890123456ULL).data();
	ProtoReader p(data);
	CHECK(p.next());
	CHECK_EQ(p.field(), 1);
	CHECK_EQ(p.varint(), uint64_t{300});
	CHECK(p.next());
	CHECK_EQ(p.field(), 2);
	CHECK_EQ(std::string(p.bytes()), std::string("hola"));
	CHECK(p.next());
	CHECK_EQ(p.varint(), uint64_t{7441234567890123456ULL});
	CHECK(!p.next());
	// 300 is the varint AC 02 (protobuf's own example).
	CHECK_EQ(ProtoWriter().varint(1, 300).data(), std::string("\x08\xac\x02", 3));
}

TEST_CASE(protobuf_reader_stops_on_truncated_data)
{
	const std::string data = ProtoWriter().bytes(1, "hello world").data();
	ProtoReader p(std::string_view(data).substr(0, 5));
	CHECK(!p.next());
	ProtoReader empty("");
	CHECK(!empty.next());
	ProtoReader bad_wire_type("\x0f"); // wire type 7 does not exist
	CHECK(!bad_wire_type.next());
}

TEST_CASE(sign_answer_gives_server_and_parameters)
{
	ProtoWriter route1, route2, route3;
	route1.bytes(1, "cursor").bytes(2, "t-1732400000000_r-1_d-1_u-1_h-1");
	route2.bytes(1, "internal_ext").bytes(2, "a b&c");
	route3.bytes(1, "empty").bytes(2, "");
	const std::string answer = ProtoWriter()
					   .bytes(5, "internal")
					   .bytes(7, route1.data())
					   .bytes(7, route2.data())
					   .bytes(7, route3.data())
					   .bytes(10, "wss://webcast16-ws-useast1a.tiktok.com/webcast/im/ws_proxy/ws_reuse_supplement/")
					   .data();
	const TikTokFetch fetch = parse_tiktok_fetch(answer);
	CHECK_EQ(fetch.push_server,
		 std::string("wss://webcast16-ws-useast1a.tiktok.com/webcast/im/ws_proxy/ws_reuse_supplement/"));
	CHECK_EQ(fetch.route_params.size(), size_t{3});
	CHECK_EQ(fetch.internal_ext, std::string("internal"));

	const std::string url = tiktok_socket_url(fetch, "7441234567890123456");
	CHECK(url.rfind(fetch.push_server + "?cursor=t-1732400000000_r-1_d-1_u-1_h-1&internal_ext=a%20b%26c&aid=1988",
			0) == 0);
	CHECK(url.find("empty=") == std::string::npos); // empty values are left out
	CHECK(url.find("&room_id=7441234567890123456&version_code=270000") != std::string::npos);
	CHECK(url.find("&compress=&") != std::string::npos);
}

TEST_CASE(sign_url_names_the_room_and_user_agent)
{
	const std::string url = tiktok_sign_url("7441234567890123456", "Mozilla/5.0 (X11)");
	CHECK_EQ(url, std::string("https://api.eulerstream.com/webcast/rooms/7441234567890123456/connect?client=obs-goal-bar"
				  "&client_enter=true&platform=web&user_agent=Mozilla%2F5.0%20%28X11%29"));
}

TEST_CASE(cookie_header_keeps_values_only)
{
	CHECK_EQ(tiktok_cookie_header("ttwid=abc; Path=/; Domain=tiktok.com; Max-Age=31536000, msToken=xyz; SameSite=None"),
		 std::string("tt-target-idc=useast1a; ttwid=abc; msToken=xyz"));
	CHECK_EQ(tiktok_cookie_header("tt-target-idc=eu-ttp2; ttwid=abc"), std::string("tt-target-idc=eu-ttp2; ttwid=abc"));
	CHECK_EQ(tiktok_cookie_header(""), std::string("tt-target-idc=useast1a"));
}

TEST_CASE(client_frames_have_the_push_frame_shape)
{
	const TikTokFrame enter = parse_tiktok_frame(tiktok_enter_room_frame(42));
	CHECK_EQ(enter.type, std::string("im_enter_room"));
	CHECK_EQ(enter.log_id, uint64_t{0});
	ProtoReader room(enter.payload);
	CHECK(room.next());
	CHECK_EQ(room.field(), 1);
	CHECK_EQ(room.varint(), uint64_t{42});

	const TikTokFrame beat = parse_tiktok_frame(tiktok_heartbeat_frame(42, 7));
	CHECK_EQ(beat.type, std::string("hb"));
	CHECK_EQ(beat.payload, ProtoWriter().varint(1, 42).varint(2, 7).data());

	TikTokFetch fetch;
	const TikTokFrame ack = parse_tiktok_frame(tiktok_ack_frame(fetch, 99));
	CHECK_EQ(ack.type, std::string("ack"));
	CHECK_EQ(ack.log_id, uint64_t{99});
	CHECK_EQ(ack.payload, std::string("-"));
	fetch.internal_ext = "ext";
	CHECK_EQ(parse_tiktok_frame(tiktok_ack_frame(fetch, 99)).payload, std::string("ext"));
}

TEST_CASE(server_frame_carries_events)
{
	const std::string fetch = ProtoWriter()
					  .bytes(1, event_bytes("WebcastChatMessage", chat("hola a todos")))
					  .bytes(1, event_bytes("WebcastGiftMessage", gift(2, 5, 1, false)))
					  .bytes(5, "ext")
					  .varint(9, 1)
					  .data();
	const std::string header = ProtoWriter().bytes(1, "compress_type").bytes(2, "none").data();
	const std::string frame =
		ProtoWriter().varint(2, 1234).bytes(5, header).bytes(6, "pb").bytes(7, "msg").bytes(8, fetch).data();
	const TikTokFrame parsed = parse_tiktok_frame(frame);
	CHECK_EQ(parsed.log_id, uint64_t{1234});
	CHECK_EQ(parsed.type, std::string("msg"));
	CHECK_EQ(parsed.compress, std::string("none"));
	const TikTokFetch inner = parse_tiktok_fetch(parsed.payload);
	CHECK(inner.need_ack);
	CHECK_EQ(inner.events.size(), size_t{2});
	CHECK_EQ(inner.events[0].method, std::string("WebcastChatMessage"));
}

TEST_CASE(comments_count_their_characters)
{
	const auto event = event_of("WebcastChatMessage", chat("  ¡hola! 🎉 "));
	CHECK(event.has_value());
	CHECK(event->is_message);
	CHECK_EQ(event->length, size_t{8});
	CHECK_EQ(event->diamonds, uint64_t{0});
}

TEST_CASE(gifts_are_worth_their_diamonds)
{
	// A gift that is not a streak counts at once: 5 diamonds × 3.
	CHECK_EQ(event_of("WebcastGiftMessage", gift(2, 5, 3, false))->diamonds, uint64_t{15});
	// A streak (type 1) counts only at its end, with the final count.
	CHECK(!event_of("WebcastGiftMessage", gift(1, 1, 4, false)).has_value());
	CHECK_EQ(event_of("WebcastGiftMessage", gift(1, 1, 5, true))->diamonds, uint64_t{5});
	// repeat 0 still means one gift.
	CHECK_EQ(event_of("WebcastGiftMessage", gift(2, 99, 0, false))->diamonds, uint64_t{99});
	CHECK(!event_of("WebcastGiftMessage", gift(2, 0, 1, false)).has_value());
	CHECK(!event_of("WebcastGiftMessage", gift(2, 5, 1, false))->is_message);
}

TEST_CASE(likes_give_the_room_total)
{
	const std::string like = ProtoWriter().varint(2, 15).varint(3, 48210).bytes(5, user("Ana")).data();
	const auto event = event_of("WebcastLikeMessage", like);
	CHECK(event.has_value());
	CHECK(event->room_likes == uint64_t{48210});
	CHECK(!event->is_message);
	CHECK(!event_of("WebcastLikeMessage", ProtoWriter().varint(2, 15).data()).has_value());
}

TEST_CASE(control_message_ends_the_stream)
{
	const auto control = [](uint64_t action) {
		return tiktok_chat_event(TikTokEvent{"WebcastControlMessage", ProtoWriter().varint(2, action).data()});
	};
	CHECK(control(3).stream_ended);
	CHECK(control(4).stream_ended);
	CHECK(!control(1).stream_ended);
	CHECK(!control(1).event.has_value());
	CHECK(!tiktok_chat_event(TikTokEvent{"WebcastMemberMessage", "x"}).event.has_value());
}

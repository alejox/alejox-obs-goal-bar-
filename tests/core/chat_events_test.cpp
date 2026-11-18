#include "core/chat_events.hpp"

#include "test_harness.hpp"

#include <string>

using namespace goalbar;

// ---- comment length -------------------------------------------------------

TEST_CASE(length_counts_characters_not_bytes)
{
	CHECK_EQ(chat_text_length("hola"), size_t{4});
	CHECK_EQ(chat_text_length("¡olé!"), size_t{5});       // 2-byte characters
	CHECK_EQ(chat_text_length("go 🎉🎉"), size_t{5});      // 4-byte emoji count once
	CHECK_EQ(chat_text_length("  hola \t\n"), size_t{4});   // ends trimmed
	CHECK_EQ(chat_text_length("a  b"), size_t{4});          // inner spaces count
	CHECK_EQ(chat_text_length(""), size_t{0});
	CHECK_EQ(chat_text_length("   "), size_t{0});
}

TEST_CASE(comment_rule_uses_the_minimum)
{
	ChatEvent event;
	event.is_message = true;
	event.length = 10;
	CHECK(counts_as_comment(event, 10));
	CHECK(counts_as_comment(event, 5));
	CHECK(!counts_as_comment(event, 11));
	event.length = 0;
	CHECK(!counts_as_comment(event, 0)); // an empty message never counts
	CHECK(!counts_as_comment(ChatEvent{}, 1)); // not a chat message
}

// ---- Twitch IRC -----------------------------------------------------------

TEST_CASE(irc_line_parses_tags_prefix_and_text)
{
	const IrcLine line = parse_irc_line(
		"@badge-info=;bits=100;display-name=Ana;emotes=;id=abc :ana!ana@ana.tmi.twitch.tv PRIVMSG #canal :hola a todos");
	CHECK_EQ(line.command, std::string("PRIVMSG"));
	CHECK_EQ(line.tags.at("bits"), std::string("100"));
	CHECK_EQ(line.tags.at("display-name"), std::string("Ana"));
	CHECK_EQ(line.params.size(), size_t{2});
	CHECK_EQ(line.params[0], std::string("#canal"));
	CHECK_EQ(line.params[1], std::string("hola a todos"));
}

TEST_CASE(irc_tags_are_unescaped)
{
	const IrcLine line = parse_irc_line("@system-msg=hola\\sque\\stal :tmi.twitch.tv USERNOTICE #c");
	CHECK_EQ(line.tags.at("system-msg"), std::string("hola que tal"));
	CHECK_EQ(line.command, std::string("USERNOTICE"));
}

TEST_CASE(twitch_message_is_a_comment_with_its_length)
{
	const auto event = twitch_chat_event(parse_irc_line(
		"@emotes=;id=1 :bob!bob@bob.tmi.twitch.tv PRIVMSG #canal :qué buen stream"));
	CHECK(event.has_value());
	CHECK(event->is_message);
	CHECK_EQ(event->length, size_t{15});
	CHECK_EQ(event->bits, uint64_t{0});
}

TEST_CASE(twitch_cheer_carries_its_bits)
{
	const auto event = twitch_chat_event(parse_irc_line(
		"@bits=250;emotes=;id=2 :ana!ana@ana.tmi.twitch.tv PRIVMSG #canal :Cheer250 vamos"));
	CHECK(event.has_value());
	CHECK_EQ(event->bits, uint64_t{250});
	CHECK(event->is_message);
}

// Twitch gives emote positions in code points; each emote counts as one character.
TEST_CASE(twitch_emotes_count_as_one_character)
{
	// "Kappa hola Kappa": two emotes (0-4 and 11-15) + " hola " → 1 + 6 + 1 = 8.
	const auto event = twitch_chat_event(parse_irc_line(
		"@emotes=25:0-4,11-15;id=3 :a!a@a.tmi.twitch.tv PRIVMSG #c :Kappa hola Kappa"));
	CHECK(event.has_value());
	CHECK_EQ(event->length, size_t{8});
	const auto only = twitch_chat_event(
		parse_irc_line("@emotes=25:0-4;id=4 :a!a@a.tmi.twitch.tv PRIVMSG #c :Kappa"));
	CHECK_EQ(only->length, size_t{1});
}

TEST_CASE(twitch_action_is_unwrapped)
{
	const auto event = twitch_chat_event(
		parse_irc_line("@emotes= :a!a@a.tmi.twitch.tv PRIVMSG #c :\x01" "ACTION baila\x01"));
	CHECK(event.has_value());
	CHECK_EQ(event->length, size_t{5});
}

TEST_CASE(twitch_non_messages_are_ignored)
{
	CHECK(!twitch_chat_event(parse_irc_line("PING :tmi.twitch.tv")).has_value());
	CHECK(!twitch_chat_event(parse_irc_line(":tmi.twitch.tv 001 justinfan1 :Welcome, GLHF!")).has_value());
	CHECK(!twitch_chat_event(parse_irc_line("@msg-id=sub :tmi.twitch.tv USERNOTICE #c")).has_value());
}

// ---- Kick (Pusher) --------------------------------------------------------

TEST_CASE(kick_chat_message_event)
{
	const std::string frame =
		R"({"event":"App\\Events\\ChatMessageEvent","data":"{\"id\":\"x\",\"content\":\"buenas noches [emote:37226:KEKW]\",\"sender\":{\"username\":\"pepe\"}}","channel":"chatrooms.1.v2"})";
	const KickFrame parsed = parse_kick_frame(frame);
	CHECK(parsed.kind == KickFrame::Kind::Chat);
	CHECK(parsed.event.is_message);
	CHECK_EQ(parsed.event.length, size_t{15}); // "buenas noches " (14) + 1 emote
}

TEST_CASE(kick_protocol_frames)
{
	CHECK(parse_kick_frame(R"({"event":"pusher:ping","data":{}})").kind == KickFrame::Kind::Ping);
	CHECK(parse_kick_frame(R"({"event":"pusher_internal:subscription_succeeded","data":"{}","channel":"chatrooms.1.v2"})")
		      .kind == KickFrame::Kind::Subscribed);
	CHECK(parse_kick_frame(R"({"event":"pusher:error","data":{"code":4001,"message":"App key not in this cluster"}})")
		      .kind == KickFrame::Kind::Error);
	CHECK(parse_kick_frame(R"({"event":"App\\Events\\UserBannedEvent","data":"{}"})").kind ==
	      KickFrame::Kind::Other);
	CHECK(parse_kick_frame("not json").kind == KickFrame::Kind::Other);
}

TEST_CASE(kick_subscribe_message_names_the_chatroom)
{
	CHECK_EQ(kick_subscribe_message(668),
		 std::string(R"({"data":{"auth":"","channel":"chatrooms.668.v2"},"event":"pusher:subscribe"})"));
}

TEST_CASE(kick_chatroom_and_pusher_settings)
{
	CHECK(kick_chatroom_id(R"({"id":1,"slug":"x","chatroom":{"id":668,"chatable_type":"App\\Models\\Channel"}})") ==
	      uint64_t{668});
	CHECK(kick_chatroom_id(R"({"chatroom":{"id":"668"}})") == uint64_t{668});
	CHECK(!kick_chatroom_id(R"({"chatroom":{"id":"abc"}})").has_value());
	CHECK(!kick_chatroom_id(R"({"slug":"x"})").has_value());
	CHECK(!kick_chatroom_id("<html>").has_value());
	const std::string realtime =
		R"({"data":{"connections":[{"provider":"pusher","credentials":{"app_key":"abc123","cluster":"us2"}}]}})";
	CHECK_EQ(kick_pusher_url(realtime),
		 std::string("wss://ws-us2.pusher.com/app/abc123?protocol=7&client=js&version=8.4.0&flash=false"));
	// Unknown answers fall back to the key Kick has used so far.
	CHECK_EQ(kick_pusher_url("{}"), std::string("wss://ws-us2.pusher.com/app/34bf7a0ff419a2a775b9?protocol=7&"
						     "client=js&version=8.4.0&flash=false"));
}

// ---- YouTube live chat ----------------------------------------------------

TEST_CASE(youtube_live_chat_page_bootstrap)
{
	const std::string html =
		R"(<script>ytcfg.set({"INNERTUBE_API_KEY":"KEY1","INNERTUBE_CONTEXT_CLIENT_VERSION":"2.20261101.00.00"});</script>)"
		R"(<script>var ytInitialData = {"continuations":[{"invalidationContinuationData":{"continuation":"CONT0","timeoutMs":5000}}]};</script>)";
	const YoutubeChatBootstrap boot = parse_youtube_chat_page(html);
	CHECK_EQ(boot.api_key, std::string("KEY1"));
	CHECK_EQ(boot.client_version, std::string("2.20261101.00.00"));
	CHECK_EQ(boot.continuation, std::string("CONT0"));
	CHECK(parse_youtube_chat_page("<html>no chat</html>").continuation.empty());
}

TEST_CASE(youtube_live_chat_batch)
{
	const std::string json = R"({"continuationContents":{"liveChatContinuation":{
		"continuations":[{"timedContinuationData":{"continuation":"CONT1","timeoutMs":4000}}],
		"actions":[
		 {"addChatItemAction":{"item":{"liveChatTextMessageRenderer":{"id":"a","message":{"runs":[{"text":"hola "},{"emoji":{"emojiId":"😀","shortcuts":[":grinning:"]}}]}}}}},
		 {"addChatItemAction":{"item":{"liveChatPaidMessageRenderer":{"id":"b","message":{"runs":[{"text":"gracias por todo"}]}}}}},
		 {"addChatItemAction":{"item":{"liveChatViewerEngagementMessageRenderer":{"id":"c"}}}},
		 {"markChatItemAsDeletedAction":{}}
		]}}})";
	const YoutubeChatBatch batch = parse_youtube_chat_batch(json);
	CHECK(batch.valid);
	CHECK_EQ(batch.continuation, std::string("CONT1"));
	CHECK_EQ(batch.timeout_ms, 4000);
	CHECK_EQ(batch.messages.size(), size_t{2});
	CHECK_EQ(batch.messages[0].length, size_t{6}); // "hola" + space + one emoji, trimmed → 6
	CHECK_EQ(batch.messages[1].length, size_t{16});
}

TEST_CASE(youtube_live_chat_end)
{
	const YoutubeChatBatch ended = parse_youtube_chat_batch(R"({"continuationContents":{"liveChatContinuation":{"actions":[]}}})");
	CHECK(ended.valid);
	CHECK(ended.continuation.empty());
	CHECK(!parse_youtube_chat_batch("{}").valid);
	CHECK(!parse_youtube_chat_batch("garbage").valid);
}

// ---- counted totals -------------------------------------------------------

TEST_CASE(count_cursor_hands_out_only_what_is_new)
{
	CountCursor cursor;
	CHECK_EQ(cursor.take(0), uint64_t{0});
	CHECK_EQ(cursor.take(3), uint64_t{3});
	CHECK_EQ(cursor.take(3), uint64_t{0});
	CHECK_EQ(cursor.take(10), uint64_t{7});
	// A new provider starts again from zero.
	cursor.reset();
	CHECK_EQ(cursor.take(2), uint64_t{2});
}

TEST_CASE(chat_tally_counts_bits_or_comments)
{
	ChatTally bits(Metric::Bits, 5);
	ChatTally comments(Metric::Comments, 5);
	ChatEvent cheer;
	cheer.is_message = true;
	cheer.length = 3;
	cheer.bits = 100;
	ChatEvent long_message;
	long_message.is_message = true;
	long_message.length = 12;
	for (const ChatEvent &event : {cheer, long_message, cheer}) {
		bits.add(event);
		comments.add(event);
	}
	CHECK_EQ(bits.total(), uint64_t{200});
	CHECK_EQ(comments.total(), uint64_t{1}); // the cheers have only 3 characters
}

// ---- paid subscriptions -----------------------------------------------------

// Seen on Kick 2026-11-18 (channel chatroom_<id>, no "App\\Events\\" prefix).
TEST_CASE(kick_gifted_subscriptions_count_each_recipient)
{
	const KickFrame frame = parse_kick_frame(
		R"({"event":"GiftedSubscriptionsEvent","data":"{\"chatroom_id\":669512,\"gifted_usernames\":[\"a\",\"b\",\"c\"],\"gifter_username\":\"g\",\"gifted_total\":3,\"gifter_total\":40,\"chunk_details\":null}","channel":"chatroom_669512"})");
	CHECK(frame.kind == KickFrame::Kind::Paid);
	CHECK_EQ(frame.event.paid_subs, uint64_t{3});
	CHECK(!frame.event.is_message);
}

TEST_CASE(kick_subscription_counts_one)
{
	const KickFrame frame = parse_kick_frame(
		R"({"event":"SubscriptionEvent","data":"{\"chatroom_id\":668,\"username\":\"ana\",\"months\":3}","channel":"chatroom_668"})");
	CHECK(frame.kind == KickFrame::Kind::Paid);
	CHECK_EQ(frame.event.paid_subs, uint64_t{1});
}

// The same kind of event on the old chatrooms.<id>.v2 channel is ignored, so a
// subscription announced on both channels is never counted twice.
TEST_CASE(kick_paid_events_only_from_the_chatroom_channel)
{
	const KickFrame frame = parse_kick_frame(
		R"({"event":"App\\Events\\SubscriptionEvent","data":"{\"chatroom_id\":668,\"username\":\"ana\",\"months\":3}","channel":"chatrooms.668.v2"})");
	CHECK(frame.kind == KickFrame::Kind::Other);
}

TEST_CASE(kick_subscribes_to_chat_and_events)
{
	const std::vector<std::string> messages = kick_subscribe_messages(668);
	CHECK_EQ(messages.size(), size_t{2});
	CHECK_EQ(messages[0], kick_subscribe_message(668));
	CHECK_EQ(messages[1], std::string(R"({"data":{"auth":"","channel":"chatroom_668"},"event":"pusher:subscribe"})"));
}

TEST_CASE(youtube_memberships_and_gift_redemptions)
{
	const std::string json = R"({"continuationContents":{"liveChatContinuation":{
		"continuations":[{"timedContinuationData":{"continuation":"C","timeoutMs":3000}}],
		"actions":[
		 {"addChatItemAction":{"item":{"liveChatMembershipItemRenderer":{"id":"m1","headerSubtext":{"simpleText":"Welcome to Canal!"}}}}},
		 {"addChatItemAction":{"item":{"liveChatMembershipItemRenderer":{"id":"m2","headerPrimaryText":{"runs":[{"text":"Member for "},{"text":"6"},{"text":" months"}]},"message":{"runs":[{"text":"seis meses ya"}]}}}}},
		 {"addChatItemAction":{"item":{"liveChatSponsorshipsGiftPurchaseAnnouncementRenderer":{"id":"g","header":{"liveChatSponsorshipsHeaderRenderer":{"primaryText":{"runs":[{"text":"Gifted "},{"text":"2"},{"text":" memberships"}]}}}}}}},
		 {"addChatItemAction":{"item":{"liveChatSponsorshipsGiftRedemptionAnnouncementRenderer":{"id":"r1","message":{"runs":[{"text":"received a gift membership by "},{"text":"g"}]}}}}},
		 {"addChatItemAction":{"item":{"liveChatSponsorshipsGiftRedemptionAnnouncementRenderer":{"id":"r2","message":{"runs":[{"text":"received a gift membership by "},{"text":"g"}]}}}}},
		 {"addChatItemAction":{"item":{"liveChatTextMessageRenderer":{"id":"t","message":{"runs":[{"text":"hola"}]}}}}}
		]}}})";
	const YoutubeChatBatch batch = parse_youtube_chat_batch(json);
	ChatTally paid(Metric::PaidSubs, 0);
	ChatTally comments(Metric::Comments, 1);
	for (const ChatEvent &event : batch.messages) {
		paid.add(event);
		comments.add(event);
	}
	// New member + renewal + two gift recipients; the purchase itself is not counted again.
	CHECK_EQ(paid.total(), uint64_t{4});
	CHECK_EQ(comments.total(), uint64_t{1}); // only "hola" is a chat message
}

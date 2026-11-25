#include "app/goal_engine.hpp"

#include "core/kick_api.hpp"
#include "core/tiktok_live.hpp"

#include "test_harness.hpp"

#include <memory>
#include <string>

using namespace goalbar;
using namespace goalbar::app;

namespace {

// A provider whose reading the test sets.
struct FakeProvider final : MetricProvider {
	FakeProvider(Platform p, Metric m, std::shared_ptr<MetricReading> r) : platform_(p), metric_(m), reading_(r) {}
	Platform platform() const override { return platform_; }
	Metric metric() const override { return metric_; }
	MetricReading latest() const override { return *reading_; }
	Platform platform_;
	Metric metric_;
	std::shared_ptr<MetricReading> reading_;
};

struct Fixture {
	std::shared_ptr<MetricReading> reading = std::make_shared<MetricReading>();
	std::shared_ptr<int> created = std::make_shared<int>(0);
	std::shared_ptr<ProviderKey> last_key = std::make_shared<ProviderKey>();

	GoalEngine::ProviderFactory factory()
	{
		return [r = reading, created = created, last = last_key](const ProviderKey &key) {
			++*created;
			*last = key;
			return std::make_unique<FakeProvider>(key.platform, key.metric, r);
		};
	}
};

MetricReading live(uint64_t value)
{
	MetricReading r;
	r.state = ProviderState::Live;
	r.value = value;
	return r;
}

OverlaySettings kick(Metric metric, uint64_t target = 100)
{
	OverlaySettings s;
	s.platform = Platform::Kick;
	s.metric = metric;
	s.kick_channel = "https://kick.com/Streamer";
	s.goal.target = target;
	return s;
}

std::string echo(const std::string &key)
{
	return key;
}

} // namespace

TEST_CASE(settings_round_trip_as_json)
{
	OverlaySettings s = kick(Metric::Comments, 4000);
	s.title = "Meta de comentarios ✨";
	s.min_chars = 10;
	s.goal.current = 1234;
	s.style = "glitch";
	s.accent = 0x80112233;
	s.port = 50000;
	s.language = "es";
	CHECK(settings_from_json(settings_to_json(s)) == s);
}

TEST_CASE(bad_settings_fall_back_one_by_one)
{
	const OverlaySettings s = settings_from_json(
		R"({"platform":"kick","metric":"likes","goal_target":0,"opacity":300,"port":80,"style":"nope","width":-5,"language":"fr","title":7})");
	CHECK(s.platform == Platform::Kick);
	CHECK(s.metric == Metric::Followers); // Kick does not read likes
	CHECK_EQ(s.goal.target, uint64_t{1});
	CHECK_EQ(s.opacity, 100);
	CHECK_EQ(s.port, uint16_t{1024});
	CHECK_EQ(s.style, std::string("minimal"));
	CHECK_EQ(s.width, uint32_t{16});
	CHECK_EQ(s.language, std::string(""));
	CHECK_EQ(s.title, std::string(""));
	CHECK(settings_from_json("not json") == OverlaySettings{});
}

TEST_CASE(provider_key_normalizes_channels)
{
	CHECK_EQ(provider_key_for(kick(Metric::Followers)).channel, std::string("streamer"));
	OverlaySettings tiktok;
	tiktok.platform = Platform::TikTok;
	tiktok.metric = Metric::Comments;
	tiktok.tiktok_channel = "@Creator";
	tiktok.min_chars = 0;
	const ProviderKey key = provider_key_for(tiktok);
	CHECK_EQ(key.channel, std::string("creator"));
	CHECK_EQ(key.min_chars, 1);
	OverlaySettings bad = kick(Metric::Followers);
	bad.kick_channel = "not a channel!";
	CHECK_EQ(provider_key_for(bad).channel, std::string(""));
}

TEST_CASE(readings_move_the_goal_and_celebrate)
{
	Fixture f;
	GoalEngine engine(f.factory());
	engine.configure(kick(Metric::Followers, 100), true);
	CHECK_EQ(*f.created, 1);
	CHECK_EQ(engine.status(), std::string("GoalBar.Status.Connecting"));

	*f.reading = live(40);
	CHECK(engine.tick(0.1));
	CHECK_EQ(engine.settings().goal.current, uint64_t{40});
	CHECK(engine.take_goal_changed());
	CHECK(!engine.take_goal_changed());
	CHECK_EQ(engine.status(), std::string(""));

	*f.reading = live(100);
	engine.tick(0.1);
	CHECK(engine.frame(echo).celebration.has_value());
	CHECK(engine.frame(echo).view.reached);
	// The celebration ends and auto next moves the goal up by the step.
	for (int i = 0; i < 60; ++i)
		engine.tick(0.1);
	CHECK(!engine.frame(echo).celebration.has_value());
	CHECK_EQ(engine.settings().goal.target, uint64_t{200});
}

TEST_CASE(counted_metrics_add_what_the_chat_adds)
{
	Fixture f;
	GoalEngine engine(f.factory());
	OverlaySettings s = kick(Metric::Followers, 100);
	s.goal.current = 70;
	engine.configure(s, true);
	*f.reading = live(70);
	engine.tick(0.1);

	// Switching to comments starts at zero, then counts the chat's additions.
	OverlaySettings comments = engine.settings();
	comments.metric = Metric::Comments;
	*f.reading = live(0);
	engine.configure(comments);
	CHECK_EQ(*f.created, 2);
	CHECK_EQ(engine.settings().goal.current, uint64_t{0});
	*f.reading = live(3);
	engine.tick(0.1);
	CHECK_EQ(engine.settings().goal.current, uint64_t{3});

	// The user types a new value: the chat keeps adding to it.
	OverlaySettings typed = engine.settings();
	typed.goal.current = 50;
	engine.configure(typed);
	CHECK_EQ(*f.created, 2); // same provider
	*f.reading = live(5);
	engine.tick(0.1);
	CHECK_EQ(engine.settings().goal.current, uint64_t{52});
}

TEST_CASE(restoring_a_reached_goal_does_not_celebrate)
{
	Fixture f;
	GoalEngine engine(f.factory());
	OverlaySettings s = kick(Metric::Followers, 100);
	s.goal.current = 150;
	engine.configure(s, true);
	engine.tick(0.1);
	CHECK(!engine.frame(echo).celebration.has_value());
}

TEST_CASE(manual_mode_and_missing_channel)
{
	Fixture f;
	GoalEngine engine(f.factory());
	OverlaySettings manual;
	manual.goal.target = 10;
	engine.configure(manual, true);
	CHECK_EQ(*f.created, 0);
	CHECK_EQ(engine.status(), std::string(""));

	// Typing the value reaches the goal: celebrates.
	OverlaySettings typed = engine.settings();
	typed.goal.current = 10;
	engine.configure(typed);
	engine.tick(0.1);
	CHECK(engine.frame(echo).celebration.has_value());

	OverlaySettings missing = kick(Metric::Followers);
	missing.kick_channel = "";
	engine.configure(missing);
	CHECK_EQ(engine.status(), std::string("GoalBar.Status.Connecting")); // the factory decides
	CHECK_EQ(f.last_key->channel, std::string(""));
}

TEST_CASE(status_and_value_label)
{
	Fixture f;
	GoalEngine engine(f.factory());
	OverlaySettings s = kick(Metric::Viewers, 2000);
	s.title = "Espectadores";
	engine.configure(s, true);
	MetricReading offline;
	offline.state = ProviderState::NotLive;
	*f.reading = offline;
	engine.tick(0.1);
	const GoalView view = engine.frame([](const std::string &) { return std::string("Sin directo"); }).view;
	CHECK_EQ(view.title, std::string("Espectadores"));
	CHECK_EQ(view.value_label, std::string("0 / 2K · Sin directo"));
}

TEST_CASE(test_celebration_never_advances)
{
	Fixture f;
	GoalEngine engine(f.factory());
	OverlaySettings manual;
	manual.goal.target = 10;
	manual.goal.current = 3;
	engine.configure(manual, true);
	engine.test_celebration();
	engine.tick(0.1);
	CHECK(engine.frame(echo).celebration.has_value());
	for (int i = 0; i < 60; ++i)
		engine.tick(0.1);
	CHECK_EQ(engine.settings().goal.target, uint64_t{10});
	CHECK(!engine.frame(echo).celebration.has_value());
}

TEST_CASE(status_keys_by_reading)
{
	MetricReading r;
	r.state = ProviderState::Live;
	CHECK_EQ(status_key(r, Platform::TikTok), std::string("GoalBar.Status.WaitingLikes"));
	CHECK_EQ(status_key(r, Platform::YouTube), std::string("GoalBar.Status.Hidden"));
	r.state = ProviderState::RateLimited;
	r.message = kTikTokSignLimit;
	CHECK_EQ(status_key(r, Platform::TikTok), std::string("GoalBar.Status.TikTokSignLimit"));
	r.state = ProviderState::Error;
	r.message = kKickBlocked;
	CHECK_EQ(status_key(r, Platform::Kick), std::string("GoalBar.Status.KickBlocked"));
}

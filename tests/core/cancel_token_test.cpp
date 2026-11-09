#include "core/cancel_token.hpp"

#include "test_harness.hpp"

using namespace goalbar;

TEST_CASE(cancel_runs_the_armed_hook_once)
{
	CancelToken token;
	int runs = 0;
	AbortHook hook(&token, [&] { ++runs; });
	REQUIRE(hook.armed());
	token.cancel();
	token.cancel();
	CHECK_EQ(runs, 1);
	CHECK(token.cancelled());
	CHECK(hook.disarm());
}

TEST_CASE(disarmed_hook_never_runs)
{
	CancelToken token;
	int runs = 0;
	{
		AbortHook hook(&token, [&] { ++runs; });
		CHECK(!hook.disarm());
	}
	token.cancel();
	CHECK_EQ(runs, 0);
}

TEST_CASE(hook_on_a_cancelled_token_is_not_armed)
{
	CancelToken token;
	token.cancel();
	int runs = 0;
	AbortHook hook(&token, [&] { ++runs; });
	CHECK(!hook.armed());
	CHECK_EQ(runs, 0);
}

TEST_CASE(null_token_arms_nothing)
{
	AbortHook hook(nullptr, [] {});
	CHECK(hook.armed());
	CHECK(!hook.disarm());
}

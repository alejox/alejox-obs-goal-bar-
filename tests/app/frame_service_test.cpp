#include "app/frame_service.hpp"

#include "test_harness.hpp"

#include <string>

using namespace goalbar::app;

namespace {

HttpRequest get(const std::string &path, const std::string &query = {})
{
	HttpRequest request;
	request.method = "GET";
	request.path = path;
	request.query = query;
	request.host = "127.0.0.1:48722";
	return request;
}

std::string header(const HttpResponse &response, const std::string &name)
{
	for (const auto &[key, value] : response.headers)
		if (key == name)
			return value;
	return {};
}

} // namespace

TEST_CASE(page_is_served_at_the_overlay_path)
{
	FrameService service("0.1.0");
	for (const char *path : {"/", "/goal-bar", "/goal-bar/"}) {
		const HttpResponse page = service.handle(get(path));
		CHECK_EQ(page.status, 200);
		CHECK(page.content_type.rfind("text/html", 0) == 0);
		CHECK(page.body.find("/goal-bar/frame?since=") != std::string::npos);
	}
	CHECK_EQ(service.handle(get("/nope")).status, 404);
	CHECK(service.handle(get("/health")).body.find("\"version\":\"0.1.0\"") != std::string::npos);
}

TEST_CASE(frames_are_sent_only_when_new)
{
	FrameService service("0.1.0");
	CHECK_EQ(service.handle(get("/goal-bar/frame", "since=0&id=a")).status, 204); // nothing painted yet
	service.publish("PNG1");
	HttpResponse frame = service.handle(get("/goal-bar/frame", "since=0&id=a"));
	CHECK_EQ(frame.status, 200);
	CHECK_EQ(frame.body, std::string("PNG1"));
	CHECK_EQ(frame.content_type, std::string("image/png"));
	const std::string version = header(frame, "X-Frame-Version");
	CHECK_EQ(version, std::string("1"));
	CHECK_EQ(service.handle(get("/goal-bar/frame", "since=1&id=a")).status, 204);
	service.publish("PNG2");
	CHECK_EQ(service.handle(get("/goal-bar/frame", "since=1&id=a")).body, std::string("PNG2"));
}

TEST_CASE(viewers_are_the_pages_asking)
{
	FrameService service("0.1.0");
	CHECK_EQ(service.viewers(), 0);
	service.handle(get("/goal-bar/frame", "since=0&id=meld"));
	service.handle(get("/goal-bar/frame", "since=0&id=meld"));
	service.handle(get("/goal-bar/frame", "since=0&id=obs"));
	service.handle(get("/goal-bar/frame", "since=0")); // no id: not counted
	CHECK_EQ(service.viewers(), 2);
}

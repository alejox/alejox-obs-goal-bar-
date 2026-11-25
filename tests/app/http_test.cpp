#include "app/http.hpp"

#include "test_harness.hpp"

#include <string>

using namespace goalbar::app;

TEST_CASE(request_head_ends_at_the_blank_line)
{
	CHECK_EQ(find_request_end("GET / HTTP/1.1\r\nHost: a\r\n"), std::string::npos);
	const std::string head = "GET / HTTP/1.1\r\nHost: a\r\n\r\nextra";
	CHECK_EQ(find_request_end(head), head.size() - 5);
}

TEST_CASE(request_line_headers_and_query)
{
	const auto request = parse_http_request(
		"GET /goal-bar/frame.png?n=12 HTTP/1.1\r\nHost: 127.0.0.1:48722\r\nUser-Agent: x\r\n\r\n");
	REQUIRE(request.has_value());
	CHECK_EQ(request->method, std::string("GET"));
	CHECK_EQ(request->path, std::string("/goal-bar/frame.png"));
	CHECK_EQ(request->query, std::string("n=12"));
	CHECK_EQ(request->host, std::string("127.0.0.1:48722"));
	CHECK(request->keep_alive);
}

TEST_CASE(connection_header_decides_keep_alive)
{
	CHECK(!parse_http_request("GET / HTTP/1.1\r\nConnection: close\r\n\r\n")->keep_alive);
	CHECK(!parse_http_request("GET / HTTP/1.0\r\n\r\n")->keep_alive);
	CHECK(parse_http_request("GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n")->keep_alive);
}

TEST_CASE(malformed_requests_are_refused)
{
	CHECK(!parse_http_request("").has_value());
	CHECK(!parse_http_request("GET\r\n\r\n").has_value());
	CHECK(!parse_http_request("GET / HTTP/2\r\n\r\n").has_value());
	CHECK(!parse_http_request("GET http://evil/ HTTP/1.1\r\n\r\n").has_value());
}

TEST_CASE(only_loopback_hosts_are_served)
{
	CHECK(is_loopback_host("127.0.0.1:48722", 48722));
	CHECK(is_loopback_host("localhost:48722", 48722));
	CHECK(is_loopback_host("localhost", 48722));
	CHECK(is_loopback_host("[::1]:48722", 48722));
	CHECK(!is_loopback_host("127.0.0.1:1234", 48722));
	CHECK(!is_loopback_host("evil.example:48722", 48722));
	CHECK(!is_loopback_host("", 48722));
}

TEST_CASE(responses_carry_length_and_no_cache)
{
	HttpResponse response;
	response.content_type = "image/png";
	response.body = "PNGDATA";
	const std::string text = format_http_response(response, false, true);
	CHECK(text.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
	CHECK(text.find("Content-Length: 7\r\n") != std::string::npos);
	CHECK(text.find("Cache-Control: no-store\r\n") != std::string::npos);
	CHECK(text.find("Connection: keep-alive\r\n") != std::string::npos);
	CHECK(text.size() > 7 && text.substr(text.size() - 7) == "PNGDATA");
	const std::string head = format_http_response(response, true, false);
	CHECK(head.find("PNGDATA") == std::string::npos);
	CHECK(head.find("Content-Length: 7\r\n") != std::string::npos);
	CHECK(head.find("Connection: close\r\n") != std::string::npos);
}

TEST_CASE(query_parameters)
{
	CHECK_EQ(query_param("since=12&id=ab3", "since"), std::string("12"));
	CHECK_EQ(query_param("since=12&id=ab3", "id"), std::string("ab3"));
	CHECK_EQ(query_param("since=12&id=ab3", "x"), std::string(""));
	CHECK_EQ(query_param("flag&since=1", "flag"), std::string(""));
	CHECK_EQ(query_param("", "since"), std::string(""));
}

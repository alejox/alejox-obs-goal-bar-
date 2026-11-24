// Network on macOS with NSURLSession (HTTPS and WebSockets, compiled with ARC).

#include "net/net.hpp"

#import <Foundation/Foundation.h>

#include <atomic>

namespace goalbar::net {

    const char *const kUserAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                                   "(KHTML, like Gecko) Chrome/130.0 Safari/537.36";

    namespace {

        constexpr NSUInteger kMaxBody = 16 * 1024 * 1024;

        NSURLSession *session()
        {
            static NSURLSession *shared = [] {
                NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
                config.HTTPAdditionalHeaders = @{@"User-Agent": @(kUserAgent)};
                config.HTTPCookieStorage = nil;
                config.URLCache = nil;
                return [NSURLSession sessionWithConfiguration:config];
            }();
            return shared;
        }

        // A WebSocket waits for messages without a time limit (a quiet chat can go minutes).
        NSURLSession *socket_session()
        {
            static NSURLSession *shared = [] {
                NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
                config.HTTPAdditionalHeaders = @{@"User-Agent": @(kUserAgent)};
                config.HTTPCookieStorage = nil;
                config.URLCache = nil;
                config.timeoutIntervalForRequest = 7 * 24 * 3600;
                return [NSURLSession sessionWithConfiguration:config];
            }();
            return shared;
        }

        std::string describe(NSError *error)
        {
            const char *text = error.localizedDescription.UTF8String;
            return text ? text : "network error";
        }

        Response perform(NSString *method, const std::string &url, const Headers &headers, const std::string *body,
                         NSString *content_type, int timeout_ms, CancelToken *cancel)
        {
            Response res;
            @autoreleasepool {
                NSURL *u = [NSURL URLWithString:@(url.c_str())];
                if (!u) {
                    res.error = "invalid URL: " + url;
                    return res;
                }
                NSMutableURLRequest *request =
                    [NSMutableURLRequest requestWithURL:u
                                            cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                        timeoutInterval:timeout_ms / 1000.0];
                request.HTTPMethod = method;
                for (const auto &[name, value] : headers)
                    [request setValue:@(value.c_str()) forHTTPHeaderField:@(name.c_str())];
                if (body) {
                    [request setValue:content_type forHTTPHeaderField:@"Content-Type"];
                    request.HTTPBody = [NSData dataWithBytes:body->data() length:body->size()];
                }

                __block Response out;
                dispatch_semaphore_t done = dispatch_semaphore_create(0);
                NSURLSessionDataTask *task = [session()
                    dataTaskWithRequest:request
                      completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                          if (error) {
                              out.error = describe(error);
                          } else if (data.length > kMaxBody) {
                              out.error = "response too large";
                          } else {
                              NSHTTPURLResponse *http = (NSHTTPURLResponse *) response;
                              out.status = (int) http.statusCode;
                              out.body.assign(static_cast<const char *>(data.bytes), data.length);
                              [http.allHeaderFields enumerateKeysAndObjectsUsingBlock:^(id key, id value, BOOL *) {
                                  if ([key isKindOfClass:[NSString class]] && [value isKindOfClass:[NSString class]])
                                      out.headers.emplace_back(((NSString *) key).UTF8String,
                                                               ((NSString *) value).UTF8String);
                              }];
                          }
                          dispatch_semaphore_signal(done);
                      }];
                // -[NSURLSessionTask cancel] is thread-safe; the completion
                // handler then runs at once with NSURLErrorCancelled.
                AbortHook hook(cancel, [task] { [task cancel]; });
                if (!hook.armed()) {
                    res.error = kCancelledError;
                    return res;
                }
                [task resume];
                dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
                res = out;
                if (hook.disarm()) {
                    res = Response{};
                    res.error = kCancelledError;
                }
            }
            return res;
        }

        class MacWebSocket : public WebSocket
        {
              public:
            explicit MacWebSocket(NSURLSessionWebSocketTask *task) : task_(task) {}
            ~MacWebSocket() override
            {
                close();
            }

            Result receive(std::string &message) override
            {
                if (closed_)
                    return Result::Closed;
                __block bool ok = false;
                __block std::string text;
                dispatch_semaphore_t done = dispatch_semaphore_create(0);
                [task_ receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *msg, NSError *error) {
                    if (!error && msg) {
                        ok = true;
                        if (msg.type == NSURLSessionWebSocketMessageTypeString) {
                            const char *utf8 = msg.string.UTF8String;
                            text = utf8 ? utf8 : "";
                        } else {
                            text.assign(static_cast<const char *>(msg.data.bytes), msg.data.length);
                        }
                    }
                    dispatch_semaphore_signal(done);
                }];
                dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
                if (!ok || closed_)
                    return Result::Closed;
                message = std::move(text);
                return Result::Message;
            }

            bool send(const std::string &text) override
            {
                if (closed_)
                    return false;
                NSURLSessionWebSocketMessage *msg =
                    [[NSURLSessionWebSocketMessage alloc] initWithString:@(text.c_str())];
                [task_ sendMessage:msg completionHandler:^(NSError *) {
                }];
                return true;
            }

            bool send_binary(const std::string &data) override
            {
                if (closed_)
                    return false;
                NSData *bytes = [NSData dataWithBytes:data.data() length:data.size()];
                NSURLSessionWebSocketMessage *msg = [[NSURLSessionWebSocketMessage alloc] initWithData:bytes];
                [task_ sendMessage:msg completionHandler:^(NSError *) {
                }];
                return true;
            }

            void close() override
            {
                // Cancelling the task completes a pending receive with an error.
                if (!closed_.exchange(true))
                    [task_ cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
            }

              private:
            NSURLSessionWebSocketTask *task_;
            std::atomic<bool> closed_ {false};
        };

    }  // namespace

    Response get(const std::string &url, const Headers &headers, int timeout_ms, CancelToken *cancel)
    {
        return perform(@"GET", url, headers, nullptr, nil, timeout_ms, cancel);
    }

    Response post_form(const std::string &url, const std::string &body, const Headers &headers, int timeout_ms,
                       CancelToken *cancel)
    {
        return perform(@"POST", url, headers, &body, @"application/x-www-form-urlencoded", timeout_ms, cancel);
    }

    Response post(const std::string &url, const std::string &body, const std::string &content_type,
                  const Headers &headers, int timeout_ms, CancelToken *cancel)
    {
        return perform(@"POST", url, headers, &body, @(content_type.c_str()), timeout_ms, cancel);
    }

    std::shared_ptr<WebSocket> connect_websocket(const std::string &url, const Headers &headers, std::string &error)
    {
        @autoreleasepool {
            NSURL *u = [NSURL URLWithString:@(url.c_str())];
            if (!u) {
                error = "invalid URL: " + url;
                return nullptr;
            }
            NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:u
                                                                   cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                               timeoutInterval:15];
            for (const auto &[name, value] : headers)
                [request setValue:@(value.c_str()) forHTTPHeaderField:@(name.c_str())];
            NSURLSessionWebSocketTask *task = [socket_session() webSocketTaskWithRequest:request];
            task.maximumMessageSize = kMaxBody;
            [task resume];
            // A failed connection shows up as Closed on the first receive().
            return std::make_shared<MacWebSocket>(task);
        }
    }

}  // namespace goalbar::net

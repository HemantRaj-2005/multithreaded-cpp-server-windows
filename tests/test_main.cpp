// ─────────────────────────────────────────────────────────────────────────────
//  tests/test_main.cpp
//  Helios Test Suite — Phase 0 + Phase 1 + Phase 2 + Phase 3
//
//  Hand-rolled test harness (no external framework).
//  See Phase 0 notes for the trade-off analysis on test frameworks.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

#include "helios/config.hpp"
#include "helios/logger.hpp"
#include "helios/version.hpp"
#include "helios/net/winsock_init.hpp"
#include "helios/net/tcp_connection.hpp"
#include "helios/net/tcp_server.hpp"
#include "helios/http/http_request.hpp"
#include "helios/http/http_response.hpp"
#include "helios/http/http_parser.hpp"
#include "helios/http/router.hpp"
#include "helios/concurrency/work_queue.hpp"
#include "helios/concurrency/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Minimal test harness
// ─────────────────────────────────────────────────────────────────────────────
namespace test {

struct Result {
    std::string name;
    bool        passed;
    std::string detail;
};

static std::vector<Result> results;

inline void check(const std::string& name, bool condition,
                  const std::string& detail = "") {
    results.push_back({name, condition, detail});
    std::cout << (condition ? "  [PASS] " : "  [FAIL] ") << name;
    if (!detail.empty()) std::cout << " — " << detail;
    std::cout << '\n';
}

inline int summarise() {
    int passed = 0, failed = 0;
    for (auto& r : results) { r.passed ? ++passed : ++failed; }
    std::cout << "\n─────────────────────────────────────────\n";
    std::cout << "  Passed: " << passed << "  Failed: " << failed << '\n';
    std::cout << "─────────────────────────────────────────\n";
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace test

// ─────────────────────────────────────────────────────────────────────────────
//  Phase 0 Tests
// ─────────────────────────────────────────────────────────────────────────────

void test_version() {
    std::cout << "\n[version]\n";
    test::check("VERSION_MAJOR == 0",
                helios::VERSION_MAJOR == 0,
                "actual: " + std::to_string(helios::VERSION_MAJOR));
    test::check("VERSION_MINOR == 1",
                helios::VERSION_MINOR == 1,
                "actual: " + std::to_string(helios::VERSION_MINOR));
    test::check("VERSION_PATCH == 0",
                helios::VERSION_PATCH == 0,
                "actual: " + std::to_string(helios::VERSION_PATCH));
    test::check("VERSION_STRING == \"0.1.0\"",
                std::string(helios::VERSION_STRING) == "0.1.0",
                "actual: " + std::string(helios::VERSION_STRING));
}

void test_config_defaults() {
    std::cout << "\n[config — defaults]\n";
    auto& cfg = helios::Config::instance();
    test::check("get_int    fallback",
                cfg.get_int("server", "port", 8080) == 8080);
    test::check("get_string fallback",
                cfg.get_string("server", "name", "helios") == "helios");
    test::check("get_bool   fallback false",
                cfg.get_bool("server", "keep_alive", false) == false);
    test::check("get_bool   fallback true",
                cfg.get_bool("server", "keep_alive", true)  == true);
}

void test_logger_level_filtering() {
    std::cout << "\n[logger — level filtering]\n";
    auto& logger = helios::Logger::instance();
    logger.set_level(helios::LogLevel::WARN);
    test::check("level() reflects WARN after set_level(WARN)",
                logger.level() == helios::LogLevel::WARN);
    bool no_throw = true;
    try {
        LOG_TRACE("test", "suppressed");
        LOG_DEBUG("test", "suppressed");
        LOG_INFO ("test", "suppressed");
        LOG_WARN ("test", "WARN visible");
        LOG_ERROR("test", "ERROR visible");
    } catch (...) { no_throw = false; }
    test::check("LOG_* macros do not throw", no_throw);
    logger.set_level(helios::LogLevel::INFO);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase 1 Tests
// ─────────────────────────────────────────────────────────────────────────────

void test_winsock_init() {
    std::cout << "\n[winsock — init]\n";
    helios::net::WinsockGuard guard;
    test::check("WinsockGuard initialises successfully", guard.ok(),
                guard.ok() ? "" : guard.error_message());
    test::check("WinsockGuard.error_message() is empty on success",
                guard.ok() ? guard.error_message().empty() : true);
}

void test_wsa_error_string() {
    std::cout << "\n[winsock — error strings]\n";
    helios::net::WinsockGuard guard;
    const std::string msg = helios::net::wsa_error_string(WSAECONNREFUSED);
    test::check("wsa_error_string(WSAECONNREFUSED) is non-empty",
                !msg.empty(), "got: \"" + msg + '"');
    test::check("wsa_error_string includes the error code",
                msg.find("10061") != std::string::npos, "got: \"" + msg + '"');
    const std::string msg2 = helios::net::wsa_error_string(WSAETIMEDOUT);
    test::check("wsa_error_string(WSAETIMEDOUT) is non-empty",
                !msg2.empty(), "got: \"" + msg2 + '"');
}

void test_tcp_server_construction() {
    std::cout << "\n[TcpServer — construction]\n";
    helios::net::WinsockGuard guard;
    {
        helios::net::TcpServer server{9090};
        test::check("TcpServer constructed with port 9090",
                    server.port() == 9090,
                    "actual port: " + std::to_string(server.port()));
        test::check("TcpServer not running before start()",
                    !server.is_running());
    }
    test::check("TcpServer destructor runs without crash", true);
}

void test_tcp_server_start_stop() {
    std::cout << "\n[TcpServer — start/stop]\n";
    helios::net::WinsockGuard guard;
    helios::net::TcpServer server{19080};
    const bool started = server.start();
    test::check("TcpServer::start() succeeds on port 19080",
                started, started ? "" : "start() returned false");
    if (started) {
        server.stop();
        test::check("TcpServer::stop() sets is_running() to false",
                    !server.is_running());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase 2 Tests
// ─────────────────────────────────────────────────────────────────────────────

void test_method_helpers() {
    std::cout << "\n[method_from_string / method_to_string]\n";
    using namespace helios::http;
    test::check("GET round-trips",
                method_from_string("GET") == Method::GET);
    test::check("POST round-trips",
                method_from_string("POST") == Method::POST);
    test::check("DELETE round-trips",
                method_from_string("DELETE") == Method::DELETE_);
    test::check("UNKNOWN for garbage",
                method_from_string("GARBAGE") == Method::UNKNOWN);
    test::check("method_to_string(GET) == \"GET\"",
                std::string(method_to_string(Method::GET)) == "GET");
    test::check("method_to_string(UNKNOWN) == \"UNKNOWN\"",
                std::string(method_to_string(Method::UNKNOWN)) == "UNKNOWN");
}

void test_parser_basic_get() {
    std::cout << "\n[HttpParser — basic GET]\n";
    using namespace helios::http;
    const std::string raw =
        "GET /hello HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "User-Agent: test/1.0\r\n"
        "Accept: */*\r\n"
        "\r\n";
    HttpRequest req;
    const ParseResult result = HttpParser::parse(raw, req);
    test::check("ParseResult::OK",            result == ParseResult::OK,  parse_result_to_string(result));
    test::check("method == GET",              req.method() == Method::GET);
    test::check("path == /hello",             req.path() == "/hello",     "actual: " + req.path());
    test::check("version == HTTP/1.1",        req.version() == "HTTP/1.1","actual: " + req.version());
    test::check("Host header parsed",         req.header("host") == "localhost:8080");
    test::check("User-Agent header parsed",   req.header("user-agent") == "test/1.0");
    test::check("body is empty for GET",      req.body().empty());
    test::check("summary() correct",          req.summary() == "GET /hello HTTP/1.1");
}

void test_parser_post_with_body() {
    std::cout << "\n[HttpParser — POST with body]\n";
    using namespace helios::http;
    const std::string body_text = "name=Alice&age=30";
    const std::string raw =
        "POST /users HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: " + std::to_string(body_text.size()) + "\r\n"
        "\r\n" + body_text;
    HttpRequest req;
    const ParseResult result = HttpParser::parse(raw, req);
    test::check("ParseResult::OK for POST",   result == ParseResult::OK, parse_result_to_string(result));
    test::check("method == POST",             req.method() == Method::POST);
    test::check("path == /users",             req.path() == "/users");
    test::check("body matches",               req.body() == body_text);
    test::check("content_length() == body",   req.content_length() == body_text.size());
}

void test_parser_empty_request() {
    std::cout << "\n[HttpParser — empty request]\n";
    using namespace helios::http;
    HttpRequest req;
    const ParseResult result = HttpParser::parse("", req);
    test::check("empty input -> EMPTY_REQUEST", result == ParseResult::EMPTY_REQUEST, parse_result_to_string(result));
}

void test_parser_missing_crlf() {
    std::cout << "\n[HttpParser — missing CRLF]\n";
    using namespace helios::http;
    HttpRequest req;
    const ParseResult result = HttpParser::parse("GET / HTTP/1.1\r\nHost: x", req);
    test::check("no \\r\\n\\r\\n -> MISSING_CRLF", result == ParseResult::MISSING_CRLF, parse_result_to_string(result));
}

void test_parser_invalid_request_line() {
    std::cout << "\n[HttpParser — invalid request line]\n";
    using namespace helios::http;
    HttpRequest req;
    const ParseResult result = HttpParser::parse("GARBAGE\r\n\r\n", req);
    test::check("single token -> INVALID_REQUEST_LINE", result == ParseResult::INVALID_REQUEST_LINE, parse_result_to_string(result));
}

void test_parser_unsupported_version() {
    std::cout << "\n[HttpParser — unsupported HTTP version]\n";
    using namespace helios::http;
    HttpRequest req;
    const ParseResult result = HttpParser::parse("GET / HTTP/2.0\r\n\r\n", req);
    test::check("HTTP/2.0 -> UNSUPPORTED_VERSION", result == ParseResult::UNSUPPORTED_VERSION, parse_result_to_string(result));
}

void test_parser_unknown_method() {
    std::cout << "\n[HttpParser — unknown method]\n";
    using namespace helios::http;
    HttpRequest req;
    const ParseResult result = HttpParser::parse("PATCH /x HTTP/1.1\r\n\r\n", req);
    test::check("PATCH parses OK with UNKNOWN method", result == ParseResult::OK, parse_result_to_string(result));
    test::check("method() == UNKNOWN", req.method() == Method::UNKNOWN);
}

void test_response_ok() {
    std::cout << "\n[HttpResponse — 200 OK]\n";
    using namespace helios::http;
    const HttpResponse res = HttpResponse::ok("Hello\r\n");
    test::check("status_code == 200", res.status_code() == 200);
    const std::string wire = res.to_string();
    test::check("wire starts with 'HTTP/1.1 200 OK'",   wire.rfind("HTTP/1.1 200 OK", 0) == 0);
    test::check("wire contains Content-Length: 7",       wire.find("Content-Length: 7") != std::string::npos);
    test::check("wire contains Connection: close",       wire.find("Connection: close") != std::string::npos);
    test::check("wire contains body",                    wire.find("Hello\r\n") != std::string::npos);
}

void test_response_not_found() {
    std::cout << "\n[HttpResponse — 404 Not Found]\n";
    using namespace helios::http;
    const HttpResponse res = HttpResponse::not_found();
    test::check("status_code == 404",  res.status_code() == 404);
    test::check("wire has 404 status", res.to_string().find("HTTP/1.1 404") != std::string::npos);
}

void test_response_builder_chaining() {
    std::cout << "\n[HttpResponse — builder chaining]\n";
    using namespace helios::http;
    const std::string wire = HttpResponse::ok("{\"ok\":true}\r\n")
                             .content_type("application/json")
                             .header("X-Custom", "test-value")
                             .to_string();
    test::check("Content-Type: application/json present", wire.find("Content-Type: application/json") != std::string::npos);
    test::check("X-Custom: test-value present",           wire.find("X-Custom: test-value") != std::string::npos);
}

void test_router_dispatch_ok() {
    std::cout << "\n[Router — dispatch 200]\n";
    using namespace helios::http;
    Router router;
    router.get("/hello", [](const HttpRequest&) { return HttpResponse::ok("hi\r\n"); });
    HttpRequest req;
    HttpParser::parse("GET /hello HTTP/1.1\r\n\r\n", req);
    test::check("GET /hello -> 200", router.dispatch(req).status_code() == 200);
}

void test_router_dispatch_404() {
    std::cout << "\n[Router — dispatch 404]\n";
    using namespace helios::http;
    Router router;
    HttpRequest req;
    HttpParser::parse("GET /missing HTTP/1.1\r\n\r\n", req);
    test::check("GET /missing -> 404", router.dispatch(req).status_code() == 404);
}

void test_router_dispatch_405() {
    std::cout << "\n[Router — dispatch 405]\n";
    using namespace helios::http;
    Router router;
    router.get("/resource", [](const HttpRequest&) { return HttpResponse::ok("data\r\n"); });
    HttpRequest req;
    HttpParser::parse("POST /resource HTTP/1.1\r\nContent-Length: 0\r\n\r\n", req);
    const HttpResponse res = router.dispatch(req);
    test::check("POST to GET-only route -> 405", res.status_code() == 405);
    test::check("405 response includes Allow header", res.to_string().find("Allow:") != std::string::npos);
}

void test_router_route_count() {
    std::cout << "\n[Router — route_count]\n";
    using namespace helios::http;
    Router router;
    test::check("empty router has 0 routes", router.route_count() == 0);
    router.get("/a", [](const HttpRequest&) { return HttpResponse::ok(); });
    router.post("/a", [](const HttpRequest&) { return HttpResponse::ok(); });
    router.get("/b", [](const HttpRequest&) { return HttpResponse::ok(); });
    test::check("3 routes registered", router.route_count() == 3,
                "actual: " + std::to_string(router.route_count()));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase 3 Tests — WorkQueue<T> and ThreadPool
// ─────────────────────────────────────────────────────────────────────────────

void test_phase3_version() {
    std::cout << "\n[version — Phase 3]\n";
    test::check("PHASE == 3",
                helios::PHASE == 3,
                "actual: " + std::to_string(helios::PHASE));
    test::check("PHASE_NAME == \"Multithreaded Thread Pool\"",
                std::string(helios::PHASE_NAME) == "Multithreaded Thread Pool",
                "actual: '" + std::string(helios::PHASE_NAME) + "'");
}

void test_work_queue_basic() {
    std::cout << "\n[WorkQueue — basic push/pop]\n";
    using helios::concurrency::WorkQueue;

    WorkQueue<int> q;
    test::check("empty() on new queue",    q.empty());
    test::check("size() == 0 on new queue", q.size() == 0);
    test::check("is_stopped() == false",   !q.is_stopped());

    q.push(42);
    test::check("size() == 1 after push",  q.size() == 1);
    test::check("empty() == false",        !q.empty());

    // pop() on a non-empty queue must return immediately (no blocking).
    // We run it on this thread — it would only block if the queue were empty.
    auto v = q.pop();
    test::check("pop() returns value",     v.has_value(),  "got nullopt");
    test::check("pop() value == 42",       v && *v == 42,  v ? std::to_string(*v) : "nullopt");
    test::check("empty() after pop",       q.empty());
}

void test_work_queue_fifo_order() {
    std::cout << "\n[WorkQueue — FIFO ordering]\n";
    using helios::concurrency::WorkQueue;

    // Verify that items are dequeued in FIFO order.
    // This is guaranteed by std::deque (push_back / pop_front).
    WorkQueue<int> q;
    for (int i = 0; i < 5; ++i) q.push(i);

    bool ordered = true;
    for (int i = 0; i < 5; ++i) {
        auto v = q.pop();
        if (!v || *v != i) { ordered = false; break; }
    }
    test::check("FIFO: items dequeued in push order", ordered);
}

void test_work_queue_stop_unblocks() {
    std::cout << "\n[WorkQueue — stop() unblocks pop()]\n";
    using helios::concurrency::WorkQueue;

    WorkQueue<int> q;
    std::atomic<bool> got_nullopt{false};

    // Launch a consumer that blocks on an empty queue.
    std::thread consumer([&q, &got_nullopt] {
        auto v = q.pop();          // blocks here until stop()
        got_nullopt.store(!v.has_value());
    });

    // Give the consumer time to enter pop() and sleep on the condvar.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    q.stop();
    consumer.join();

    test::check("stop() causes pop() to return nullopt", got_nullopt.load());
    test::check("is_stopped() == true after stop()",     q.is_stopped());
}

void test_work_queue_push_after_stop() {
    std::cout << "\n[WorkQueue — push() after stop() is discarded]\n";
    using helios::concurrency::WorkQueue;

    WorkQueue<int> q;
    q.stop();
    q.push(99);   // must be silently discarded
    test::check("queue empty after push()-post-stop", q.empty());
}

void test_work_queue_mpsc() {
    std::cout << "\n[WorkQueue — multi-producer, single-consumer]\n";
    using helios::concurrency::WorkQueue;

    // 4 producers x 250 items = 1000 total.
    // The consumer pops until stop() + empty.
    WorkQueue<int> q;
    constexpr int PRODUCERS  = 4;
    constexpr int ITEMS_EACH = 250;
    constexpr int TOTAL      = PRODUCERS * ITEMS_EACH;

    std::atomic<int> consumed{0};

    std::thread consumer([&] {
        while (true) {
            auto v = q.pop();
            if (!v) break;
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::vector<std::thread> producers;
    producers.reserve(PRODUCERS);
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&q] {
            for (int i = 0; i < ITEMS_EACH; ++i) q.push(i);
        });
    }

    for (auto& t : producers) t.join();
    q.stop();
    consumer.join();

    test::check("all " + std::to_string(TOTAL) + " items consumed",
                consumed.load() == TOTAL,
                "consumed: " + std::to_string(consumed.load()));
}

void test_thread_pool_construction() {
    std::cout << "\n[ThreadPool — construction / graceful destruction]\n";
    using helios::concurrency::ThreadPool;

    // Construct with 2 workers, destroy immediately.
    // Workers start → block on empty queue → stop() → join.
    {
        ThreadPool pool{2, [](helios::net::TcpConnection) {}};
        test::check("thread_count() == 2",         pool.thread_count() == 2);
        test::check("queue_size() == 0 initially", pool.queue_size() == 0);
        // ~ThreadPool() here: stops queue, joins both workers
    }
    test::check("ThreadPool destructor completes without deadlock", true);
}

void test_thread_pool_thread_count_clamp() {
    std::cout << "\n[ThreadPool — thread_count clamp to 1 for 0 input]\n";
    using helios::concurrency::ThreadPool;

    // Passing 0 must clamp to 1 (std::max(1u, 0u) in the constructor).
    ThreadPool pool{0, [](helios::net::TcpConnection) {}};
    test::check("0 workers clamped to 1", pool.thread_count() == 1,
                "actual: " + std::to_string(pool.thread_count()));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "═══════════════════════════════════════════\n";
    std::cout << "  Helios Test Suite — Phase 0 + 1 + 2 + 3\n";
    std::cout << "═══════════════════════════════════════════\n";

    // Phase 0
    test_version();
    test_config_defaults();
    test_logger_level_filtering();

    // Phase 1
    test_winsock_init();
    test_wsa_error_string();
    test_tcp_server_construction();
    test_tcp_server_start_stop();

    // Phase 2
    test_method_helpers();
    test_parser_basic_get();
    test_parser_post_with_body();
    test_parser_empty_request();
    test_parser_missing_crlf();
    test_parser_invalid_request_line();
    test_parser_unsupported_version();
    test_parser_unknown_method();
    test_response_ok();
    test_response_not_found();
    test_response_builder_chaining();
    test_router_dispatch_ok();
    test_router_dispatch_404();
    test_router_dispatch_405();
    test_router_route_count();

    // Phase 3
    test_phase3_version();
    test_work_queue_basic();
    test_work_queue_fifo_order();
    test_work_queue_stop_unblocks();
    test_work_queue_push_after_stop();
    test_work_queue_mpsc();
    test_thread_pool_construction();
    test_thread_pool_thread_count_clamp();

    return test::summarise();
}

// ─────────────────────────────────────────────────────────────────────────────
//  src/main.cpp
//  Helios HTTP Server — Entry Point
//
//  Startup sequence:
//    1. Print banner
//    2. Load config
//    3. Initialise logger
//    4. Initialise Winsock
//    5. Register Ctrl+C handler
//    6. Build the Router (register routes)
//    7. Create ThreadPool (N worker threads)
//    8. Create and start TcpServer
//    9. Run accept loop (blocking) → each accepted conn → pool.submit()
//   10. Graceful shutdown (Ctrl+C → stop server → join pool)
//
//  Phase 3 change:
//    The accept loop now calls pool.submit(std::move(conn)) instead of
//    handle_http_request(conn) directly.  Workers in the ThreadPool call
//    handle_http_request(router, conn) concurrently.
//
//    TcpServer::run() signature is unchanged (ConnectionHandler lambda).
//    The lambda now captures the pool and submits work rather than handling
//    it inline.  No changes to TcpServer, HttpParser, Router, or any Phase 2
//    code — the upgrade is exactly as predicted in the Phase 1 LLD comment.
// ─────────────────────────────────────────────────────────────────────────────

// winsock2.h MUST precede any Windows headers to avoid winsock.h conflicts.
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
#include "helios/http/http_parser.hpp"
#include "helios/http/http_request.hpp"
#include "helios/http/http_response.hpp"
#include "helios/http/router.hpp"
#include "helios/concurrency/thread_pool.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>   // std::thread::hardware_concurrency

// ─────────────────────────────────────────────────────────────────────────────
//  Process-level shutdown state
//
//  SetConsoleCtrlHandler requires a plain free function.  g_server is the only
//  global mutable state in Helios.  It is a raw non-owning pointer because:
//    • The server object is owned by main()'s stack frame.
//    • The pointer is valid for the entire lifetime of run().
//    • It is only written once (before run()) and read once (in the handler).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

helios::net::TcpServer* g_server = nullptr;

BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        LOG_INFO("main", "Shutdown signal received — stopping server...");
        if (g_server) {
            g_server->stop();  // sets running_ = false (std::atomic — safe here)
        }
        return TRUE;
    }
    return FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
void print_banner() {
    std::cout <<
        "\n"
        "  +------------------------------------------------+\n"
        "  |                                                |\n"
        "  |        H E L I O S   H T T P   S E R V E R    |\n"
        "  |        Production-grade C++17 HTTP Server      |\n"
        "  |                                                |\n"
        "  +------------------------------------------------+\n"
        "\n";
}

helios::LogLevel parse_log_level(const std::string& s) {
    if (s == "TRACE") return helios::LogLevel::TRACE;
    if (s == "DEBUG") return helios::LogLevel::DEBUG;
    if (s == "WARN")  return helios::LogLevel::WARN;
    if (s == "ERROR") return helios::LogLevel::ERR;
    if (s == "FATAL") return helios::LogLevel::FATAL_;
    return helios::LogLevel::INFO;
}

// ─────────────────────────────────────────────────────────────────────────────
//  build_router() — unchanged from Phase 2.
//  Routes: GET /, GET /health, GET /hello
// ─────────────────────────────────────────────────────────────────────────────
helios::http::Router build_router() {
    using namespace helios::http;
    Router router;

    router.get("/", [](const HttpRequest&) {
        const std::string body =
            "<!DOCTYPE html>\r\n"
            "<html><head><title>Helios HTTP Server</title></head>\r\n"
            "<body>\r\n"
            "<h1>Helios HTTP Server</h1>\r\n"
            "<p>Phase 3 \xe2\x80\x94 Multithreaded Thread Pool</p>\r\n"
            "<ul>\r\n"
            "  <li><a href=\"/health\">/health</a></li>\r\n"
            "  <li><a href=\"/hello\">/hello</a></li>\r\n"
            "</ul>\r\n"
            "</body></html>\r\n";
        return HttpResponse::ok(body)
               .content_type("text/html; charset=utf-8");
    });

    router.get("/health", [](const HttpRequest&) {
        const std::string body =
            "{\"status\":\"ok\","
            "\"server\":\"Helios\","
            "\"version\":\"" + std::string(helios::VERSION_STRING) + "\"}\r\n";
        return HttpResponse::ok(body)
               .content_type("application/json");
    });

    router.get("/hello", [](const HttpRequest&) {
        return HttpResponse::ok("Hello from Helios!\r\n")
               .content_type("text/plain");
    });

    return router;
}

// ─────────────────────────────────────────────────────────────────────────────
//  handle_http_request() — unchanged from Phase 2.
//
//  Called concurrently by multiple worker threads.
//
//  Thread-safety analysis:
//    • router      — read-only after startup → no lock needed.
//    • Logger      — uses std::mutex internally → safe.
//    • conn        — exclusively owned by this worker (moved in) → safe.
//    • No global mutable state is accessed here.
// ─────────────────────────────────────────────────────────────────────────────
void handle_http_request(const helios::http::Router& router,
                         helios::net::TcpConnection  conn) {
    using namespace helios::http;

    const std::string peer = conn.peer_address();

    const std::string raw = conn.read_request();
    if (raw.empty()) {
        LOG_WARN("http", peer + " \xe2\x80\x94 empty request");
        return;
    }

    HttpRequest  req;
    const ParseResult result = HttpParser::parse(raw, req);
    HttpResponse res;

    if (result != ParseResult::OK) {
        LOG_WARN("http", peer + " \xe2\x80\x94 parse error: " +
                 parse_result_to_string(result));
        res = HttpResponse::bad_request(
            "400 Bad Request: " + std::string(parse_result_to_string(result)) + "\r\n");
    } else {
        res = router.dispatch(req);
        LOG_INFO("http",
                 peer + "  " + req.summary() + " " +
                 std::to_string(res.status_code()));
    }

    if (!conn.write(res.to_string())) {
        LOG_WARN("http", peer + " \xe2\x80\x94 write failed");
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
//  main()
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {

    // ── 1. Banner ─────────────────────────────────────────────────────────
    print_banner();

    // ── 2. Config ─────────────────────────────────────────────────────────
    const char* config_path = (argc >= 2) ? argv[1] : "config/server.conf";
    auto& cfg = helios::Config::instance();
    const bool cfg_loaded = cfg.load(config_path);

    // ── 3. Logger ─────────────────────────────────────────────────────────
    const std::string level_str = cfg.get_string("logging", "level", "INFO");
    helios::Logger::instance().set_level(parse_log_level(level_str));

    if (cfg_loaded) {
        LOG_INFO("main", "Config loaded from: " + std::string(config_path));
    } else {
        LOG_WARN("main", std::string("Config not found at '") + config_path +
                         "' \xe2\x80\x94 using defaults");
    }

    // ── Determine thread count ────────────────────────────────────────────
    // Priority:
    //   1. config [server] worker_threads  (explicit user setting)
    //   2. std::thread::hardware_concurrency()  (OS-reported logical cores)
    //   3. Fallback to 4 if hardware_concurrency() returns 0
    //
    // Why not always use hardware_concurrency()?
    //   It can return 0 on systems where the value is not computable.
    //   The fallback guards against a zero-thread pool.
    //
    // Why hardware_concurrency() as the default?
    //   For I/O-bound workloads, one worker per logical core is a reasonable
    //   starting point.  Phase 7 benchmarks will tell us the optimal multiple.
    const unsigned int hw_threads =
        std::max(1u, std::thread::hardware_concurrency());
    const unsigned int worker_threads =
        static_cast<unsigned int>(
            cfg.get_int("server", "worker_threads",
                        static_cast<int>(hw_threads)));

    const std::string sep(52, '=');
    LOG_INFO("main", sep);
    LOG_INFO("main", "  Version        : " + std::string(helios::VERSION_STRING));
    LOG_INFO("main", "  Phase          : " + std::to_string(helios::PHASE)
                                           + " \xe2\x80\x94 " + helios::PHASE_NAME);
    LOG_INFO("main", "  Log level      : " + level_str);
    LOG_INFO("main", "  Worker threads : " + std::to_string(worker_threads));
    LOG_INFO("main", sep);

    // ── 4. Winsock ────────────────────────────────────────────────────────
    helios::net::WinsockGuard winsock;
    if (!winsock.ok()) {
        LOG_FATAL("main", "Winsock init failed: " + winsock.error_message());
        return EXIT_FAILURE;
    }
    LOG_INFO("main", "Winsock 2.2 initialised");

    // ── 5. Ctrl+C handler ─────────────────────────────────────────────────
    ::SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    // ── 6. Router ─────────────────────────────────────────────────────────
    helios::http::Router router = build_router();
    LOG_INFO("main", "Router initialised \xe2\x80\x94 " +
             std::to_string(router.route_count()) + " route(s) registered");

    // ── 7. Thread pool ────────────────────────────────────────────────────
    //
    // The pool is constructed here, BEFORE the accept loop starts.
    // Workers begin blocking on WorkQueue::pop() immediately.
    //
    // Capture router by const ref inside the handler lambda.
    // This is safe because:
    //   • router outlives the pool (both on main()'s stack).
    //   • router is read-only after construction.
    //   • Workers only read the route table — no mutation, no locking needed.
    helios::concurrency::ThreadPool pool{
        worker_threads,
        [&router](helios::net::TcpConnection conn) {
            handle_http_request(router, std::move(conn));
        }
    };

    // ── 8. TcpServer ──────────────────────────────────────────────────────
    const int port = cfg.get_int("server", "port", 8080);
    helios::net::TcpServer server{port};
    g_server = &server;

    if (!server.start()) {
        LOG_FATAL("main",
                  "Server failed to start on port " + std::to_string(port) +
                  ". Is another process using this port?");
        return EXIT_FAILURE;
    }

    LOG_INFO("main", "Listening \xe2\x86\x92  http://localhost:" + std::to_string(port) + "/");
    LOG_INFO("main", "Press Ctrl+C to stop.");

    // ── 9. Accept loop ────────────────────────────────────────────────────
    //
    // Phase 3 change: the lambda now SUBMITS work to the pool instead of
    // handling it inline.  The accept thread is never blocked by HTTP
    // processing — it immediately loops back to select() for the next client.
    //
    // Phase 2 (single-threaded):
    //   server.run([&router](TcpConnection conn) {
    //       handle_http_request(router, std::move(conn));   // BLOCKING
    //   });
    //
    // Phase 3 (thread pool):
    //   server.run([&pool](TcpConnection conn) {
    //       pool.submit(std::move(conn));   // NON-BLOCKING: microseconds
    //   });
    server.run([&pool](helios::net::TcpConnection conn) {
        pool.submit(std::move(conn));
    });

    // ── 10. Graceful shutdown ─────────────────────────────────────────────
    //
    // Shutdown sequence (triggered by Ctrl+C → console_ctrl_handler → server.stop()):
    //
    //   1. server.run() returns (accept loop exited).
    //   2. g_server = nullptr (null the signal-handler pointer).
    //   3. pool goes out of scope → ~ThreadPool() runs:
    //        a. WorkQueue::stop() — wakes all blocked workers.
    //        b. Joins every worker thread — waits for in-flight requests.
    //   4. router goes out of scope.
    //   5. winsock goes out of scope → WSACleanup().
    //
    // In-flight connections at shutdown time:
    //   Connections already dequeued and being processed by workers run to
    //   completion (the join() waits for them).  Connections still in the queue
    //   are discarded (their TcpConnection destructors close the sockets).
    //   This is acceptable for a development server; Phase 5 will add a
    //   drain-before-shutdown mode.
    g_server = nullptr;
    LOG_INFO("main", "Helios stopped. Goodbye.");
    // pool destructor runs here → joins workers
    return EXIT_SUCCESS;
}

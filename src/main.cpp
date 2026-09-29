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
//    7. Create and start TcpServer
//    8. Run accept loop (blocking)
//    9. Graceful shutdown
//
//  Phase 2 change:
//    handle_http_request() now uses HttpParser → Router → HttpResponse
//    instead of the hardcoded "Hello from Helios!" stub from Phase 1.
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

#include <cstdlib>
#include <iostream>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Process-level shutdown state
//
//  SetConsoleCtrlHandler requires a plain free function — it cannot capture
//  variables from main().  g_server is the only global mutable state in
//  Helios.  It is a raw non-owning pointer because:
//    • The server object is owned by main()'s stack frame.
//    • The pointer is valid for the entire lifetime of run().
//    • It is only written once (before run()) and read once (in the handler).
//
//  This is the standard Windows pattern for console signal handling.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

helios::net::TcpServer* g_server = nullptr;

// Called by Windows on Ctrl+C, Ctrl+Break, or console close.
// Runs on a separate OS-managed thread — only call async-signal-safe code.
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        LOG_INFO("main", "Shutdown signal received — stopping server...");
        if (g_server) {
            g_server->stop();  // sets running_ = false (std::atomic — safe here)
        }
        return TRUE;  // handled; suppress the default handler (process exit)
    }
    return FALSE;  // let other events (CTRL_CLOSE_EVENT etc.) be handled normally
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
//  build_router()
//
//  Register all Phase 2 routes.  The router is built once during startup
//  and then passed (by const ref) into the connection handler closure.
//
//  Routes registered here:
//    GET /       → HTML homepage
//    GET /health → plain-text health check (used by load balancers in Phase 13)
//    GET /hello  → plain-text greeting
//
//  Phase 4 will add: GET /users, GET /users/{id}, POST /users, DELETE /users/{id}
// ─────────────────────────────────────────────────────────────────────────────
helios::http::Router build_router() {
    using namespace helios::http;
    Router router;

    // ── GET / ─────────────────────────────────────────────────────────────
    router.get("/", [](const HttpRequest&) {
        const std::string body =
            "<!DOCTYPE html>\r\n"
            "<html><head><title>Helios HTTP Server</title></head>\r\n"
            "<body>\r\n"
            "<h1>Helios HTTP Server</h1>\r\n"
            "<p>Phase 2 — HTTP Abstraction Layer</p>\r\n"
            "<ul>\r\n"
            "  <li><a href=\"/health\">/health</a></li>\r\n"
            "  <li><a href=\"/hello\">/hello</a></li>\r\n"
            "</ul>\r\n"
            "</body></html>\r\n";
        return HttpResponse::ok(body)
               .content_type("text/html; charset=utf-8");
    });

    // ── GET /health ───────────────────────────────────────────────────────
    // Standard health-check endpoint.
    // Returns 200 + JSON body when healthy.
    // Phase 13 will use this for load-balancer health probes.
    router.get("/health", [](const HttpRequest&) {
        const std::string body =
            "{\"status\":\"ok\","
            "\"server\":\"Helios\","
            "\"version\":\"" + std::string(helios::VERSION_STRING) + "\"}\r\n";
        return HttpResponse::ok(body)
               .content_type("application/json");
    });

    // ── GET /hello ────────────────────────────────────────────────────────
    router.get("/hello", [](const HttpRequest&) {
        return HttpResponse::ok("Hello from Helios!\r\n")
               .content_type("text/plain");
    });

    return router;
}

// ─────────────────────────────────────────────────────────────────────────────
//  handle_http_request()
//
//  Phase 2 implementation:
//    1. Read raw bytes (TcpConnection::read_request — unchanged from Phase 1).
//    2. Parse with HttpParser → HttpRequest.
//    3. Dispatch through Router → HttpResponse.
//    4. Serialise and write back over the socket.
//    5. Log: "GET /hello 200" or "GET /bad 404".
//
//  The router is captured by const ref — no copies, no overhead.
//  The connection is taken by value (RAII move — closes socket on return).
// ─────────────────────────────────────────────────────────────────────────────
void handle_http_request(helios::http::Router const& router,
                         helios::net::TcpConnection  conn) {
    using namespace helios::http;

    const std::string peer = conn.peer_address();

    // ── Read raw request bytes ────────────────────────────────────────────
    const std::string raw = conn.read_request();
    if (raw.empty()) {
        LOG_WARN("http", peer + " — empty request (client disconnected or timed out)");
        return;
    }

    // ── Parse ─────────────────────────────────────────────────────────────
    HttpRequest  req;
    const ParseResult parse_result = HttpParser::parse(raw, req);

    HttpResponse res;

    if (parse_result != ParseResult::OK) {
        // Parsing failed — return 400 Bad Request.
        const std::string detail = parse_result_to_string(parse_result);
        LOG_WARN("http", peer + " — parse error: " + detail);
        res = HttpResponse::bad_request("400 Bad Request: " + detail + "\r\n");
    } else {
        // ── Dispatch ──────────────────────────────────────────────────────
        res = router.dispatch(req);

        // ── Access log ────────────────────────────────────────────────────
        // Format: "127.0.0.1:54321  GET /hello 200"
        LOG_INFO("http",
                 peer + "  " + req.summary() +
                 " " + std::to_string(res.status_code()));
    }

    // ── Write response ────────────────────────────────────────────────────
    if (!conn.write(res.to_string())) {
        LOG_WARN("http", peer + " — write failed (client may have disconnected)");
    }

    // conn goes out of scope → ~TcpConnection() → shutdown(SD_SEND) + closesocket()
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
                         "' — using defaults");
    }

    const std::string sep(52, '=');
    LOG_INFO("main", sep);
    LOG_INFO("main", "  Version   : " + std::string(helios::VERSION_STRING));
    LOG_INFO("main", "  Phase     : " + std::to_string(helios::PHASE)
                                      + " — " + helios::PHASE_NAME);
    LOG_INFO("main", "  Log level : " + level_str);
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

    // ── 6. Build router ───────────────────────────────────────────────────
    helios::http::Router router = build_router();
    LOG_INFO("main", "Router initialised — " +
             std::to_string(router.route_count()) + " route(s) registered");

    // ── 7. Create and start server ────────────────────────────────────────
    const int port = cfg.get_int("server", "port", 8080);
    helios::net::TcpServer server{port};
    g_server = &server;  // expose to signal handler (non-owning)

    if (!server.start()) {
        LOG_FATAL("main",
                  "Server failed to start on port " + std::to_string(port) +
                  ". Is another process using this port?");
        return EXIT_FAILURE;
    }

    LOG_INFO("main", "Listening →  http://localhost:" + std::to_string(port) + "/");
    LOG_INFO("main", "Press Ctrl+C to stop.");

    // ── 8. Accept loop (blocks until stop() is called) ────────────────────
    //
    // The lambda captures router by const ref.  This is safe because:
    //   • router outlives the accept loop (both live in main's stack frame).
    //   • In Phase 3, the router will be captured by const ref inside each
    //     worker thread closure — still valid as long as we don't mutate
    //     the route table at runtime (which we don't until Phase 4).
    server.run([&router](helios::net::TcpConnection conn) {
        handle_http_request(router, std::move(conn));
    });

    // ── 9. Shutdown ───────────────────────────────────────────────────────
    g_server = nullptr;  // null before server goes out of scope
    LOG_INFO("main", "Helios stopped. Goodbye.");

    return EXIT_SUCCESS;
    // Destructors fire here (LIFO):
    //   ~TcpServer()      → closesocket(server_fd_)
    //   ~WinsockGuard()   → WSACleanup()
    //   ~Config()         → no-op
    //   ~Logger()         → no-op
}

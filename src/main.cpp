// ─────────────────────────────────────────────────────────────────────────────
//  src/main.cpp
//  Helios HTTP Server — Entry Point
//
//  Startup sequence (updated each phase):
//    1. Print banner
//    2. Load config
//    3. Initialise logger
//    4. Initialise Winsock        ← Phase 1
//    5. Register Ctrl+C handler   ← Phase 1
//    6. Create and start TcpServer ← Phase 1
//    7. Run accept loop (blocking) ← Phase 1
//    8. Graceful shutdown
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
// std::atomic::store() is async-signal-safe; LOG_INFO uses a mutex which is
// technically not, but is acceptable for a clean shutdown notification.
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
//  handle_http_request
//
//  Phase 1 implementation:
//    • Read raw request bytes until end of headers
//    • Log the request line ("GET / HTTP/1.1")
//    • Send a hardcoded HTTP/1.1 200 response
//
//  Phase 2 upgrade: replace with HttpParser → Router → Handler dispatch.
//
//  The function takes TcpConnection by value (moved in).
//  When this function returns, conn's destructor fires → socket is closed.
// ─────────────────────────────────────────────────────────────────────────────
void handle_http_request(helios::net::TcpConnection conn) {
    const std::string peer = conn.peer_address();

    // Read until \r\n\r\n (end of HTTP request headers)
    const std::string raw = conn.read_request();

    if (raw.empty()) {
        LOG_WARN("http", peer + " — empty request (client disconnected or timed out)");
        return;
    }

    // Extract and log the request line (e.g. "GET /hello HTTP/1.1")
    const auto line_end = raw.find("\r\n");
    const std::string request_line =
        (line_end != std::string::npos) ? raw.substr(0, line_end) : raw;
    LOG_INFO("http", peer + "  " + request_line);

    // ── Build the HTTP/1.1 response ───────────────────────────────────────
    //
    // HTTP/1.1 response format (RFC 7230):
    //
    //   HTTP/1.1 200 OK\r\n          ← Status-Line: version SP status SP reason
    //   Content-Type: text/plain\r\n ← Entity headers
    //   Content-Length: 20\r\n       ← MUST be exact byte count of body
    //   Connection: close\r\n        ← Phase 1: no keep-alive
    //   Server: Helios/1.0.0\r\n     ← Server identification
    //   \r\n                         ← Blank line: end of headers
    //   Hello from Helios!\r\n       ← Body (exactly Content-Length bytes)
    //
    // Connection: close tells the client we will close the socket after
    // sending this response.  HTTP Keep-Alive (reusing the connection for
    // multiple requests) is implemented in Phase 5.
    const std::string body = "Hello from Helios!\r\n";

    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "Server: Helios/" + std::string(helios::VERSION_STRING) + "\r\n"
        "\r\n" +
        body;

    if (!conn.write(response)) {
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
    // WinsockGuard constructor calls WSAStartup(2.2).
    // Its destructor calls WSACleanup() — runs automatically when main() exits.
    helios::net::WinsockGuard winsock;
    if (!winsock.ok()) {
        LOG_FATAL("main", "Winsock init failed: " + winsock.error_message());
        return EXIT_FAILURE;
    }
    LOG_INFO("main", "Winsock 2.2 initialised");

    // ── 5. Ctrl+C handler ─────────────────────────────────────────────────
    ::SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    // ── 6. Create and start server ────────────────────────────────────────
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

    // ── 7. Accept loop (blocks until stop() is called) ────────────────────
    server.run(handle_http_request);

    // ── 8. Shutdown ───────────────────────────────────────────────────────
    g_server = nullptr;  // null before server goes out of scope
    LOG_INFO("main", "Helios stopped. Goodbye.");

    return EXIT_SUCCESS;
    // Destructors fire here (LIFO):
    //   ~TcpServer()      → closesocket(server_fd_)
    //   ~WinsockGuard()   → WSACleanup()
    //   ~Config()         → no-op
    //   ~Logger()         → no-op
}

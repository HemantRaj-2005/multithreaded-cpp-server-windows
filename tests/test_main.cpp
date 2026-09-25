// ─────────────────────────────────────────────────────────────────────────────
//  tests/test_main.cpp
//  Helios Test Suite — Phase 0 + Phase 1
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

#include <cstdlib>
#include <iostream>
#include <string>
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
    test::check("PHASE == 1",
                helios::PHASE == 1,
                "actual: " + std::to_string(helios::PHASE));
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

    // Winsock version check — WSAStartup(2.2) should always succeed on
    // Windows XP+.
    test::check("WinsockGuard.error_message() is empty on success",
                guard.ok() ? guard.error_message().empty() : true);
}

void test_wsa_error_string() {
    std::cout << "\n[winsock — error strings]\n";

    // We need Winsock initialised to use WSAGetLastError and FormatMessage.
    helios::net::WinsockGuard guard;

    // WSAECONNREFUSED (10061): "Connection refused"
    const std::string msg = helios::net::wsa_error_string(WSAECONNREFUSED);
    test::check("wsa_error_string(WSAECONNREFUSED) is non-empty",
                !msg.empty(), "got: \"" + msg + '"');
    test::check("wsa_error_string includes the error code",
                msg.find("10061") != std::string::npos, "got: \"" + msg + '"');

    // WSAETIMEDOUT (10060): "Connection timed out"
    const std::string msg2 = helios::net::wsa_error_string(WSAETIMEDOUT);
    test::check("wsa_error_string(WSAETIMEDOUT) is non-empty",
                !msg2.empty(), "got: \"" + msg2 + '"');
}

void test_tcp_server_construction() {
    std::cout << "\n[TcpServer — construction]\n";

    helios::net::WinsockGuard guard;

    // Construct without calling start() — should not crash or leak.
    {
        helios::net::TcpServer server{9090};
        test::check("TcpServer constructed with port 9090",
                    server.port() == 9090,
                    "actual port: " + std::to_string(server.port()));
        test::check("TcpServer not running before start()",
                    !server.is_running());
        // server destructor fires here — should be a no-op (never start()-ed)
    }
    test::check("TcpServer destructor runs without crash", true);
}

void test_tcp_server_start_stop() {
    std::cout << "\n[TcpServer — start/stop]\n";

    helios::net::WinsockGuard guard;

    helios::net::TcpServer server{19080}; // high port, unlikely to conflict
    const bool started = server.start();

    test::check("TcpServer::start() succeeds on port 19080",
                started, started ? "" : "start() returned false");

    if (started) {
        // Stop before run() to verify stop() works independently.
        server.stop();
        test::check("TcpServer::stop() sets is_running() to false",
                    !server.is_running());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "═══════════════════════════════════════════\n";
    std::cout << "  Helios Test Suite — Phase 0 + Phase 1\n";
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

    return test::summarise();
}

// ─────────────────────────────────────────────────────────────────────────────
//  tests/test_main.cpp
//  Phase 0: Placeholder test runner.
//
//  Minimal hand-rolled test harness. External test frameworks (GoogleTest,
//  Catch2) will be evaluated and possibly introduced in Phase 1 or Phase 2.
//  When that happens we document the trade-off here:
//
//  Trade-offs of adding a test framework:
//    Gain: expressive assertions, parameterised tests, test filtering,
//          XML/JUnit output for CI.
//    Lose: one more external dependency; larger build.
//
//  For Phase 0, a simple pass/fail counter is sufficient to verify the build
//  pipeline works end-to-end.
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/config.hpp"
#include "helios/logger.hpp"
#include "helios/version.hpp"

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
    for (auto& r : results) { (r.passed ? passed : failed)++; }
    std::cout << "\n─────────────────────────────────────────\n";
    std::cout << "  Passed: " << passed << "  Failed: " << failed << '\n';
    std::cout << "─────────────────────────────────────────\n";
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace test

// ─────────────────────────────────────────────────────────────────────────────
//  Tests
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
    test::check("PHASE == 0",
                helios::PHASE == 0,
                "actual: " + std::to_string(helios::PHASE));
}

void test_config_defaults() {
    std::cout << "\n[config — defaults]\n";
    auto& cfg = helios::Config::instance();

    // Config not loaded — every get_* should return its default.
    test::check("get_int fallback to default",
                cfg.get_int("server", "port", 8080) == 8080);
    test::check("get_string fallback to default",
                cfg.get_string("server", "name", "helios") == "helios");
    test::check("get_bool fallback to default (false)",
                cfg.get_bool("server", "keep_alive", false) == false);
    test::check("get_bool fallback to default (true)",
                cfg.get_bool("server", "keep_alive", true) == true);
}

void test_logger_level_filtering() {
    std::cout << "\n[logger — level filtering]\n";
    auto& logger = helios::Logger::instance();

    logger.set_level(helios::LogLevel::WARN);
    test::check("level() reflects WARN after set_level(WARN)",
                logger.level() == helios::LogLevel::WARN);

    // We cannot easily capture stderr in this lightweight harness, so we
    // just verify that the calls compile and do not throw.
    // Full output verification will come with a proper test framework.
    bool no_throw = true;
    try {
        LOG_TRACE("test", "should be suppressed");
        LOG_DEBUG("test", "should be suppressed");
        LOG_INFO ("test", "should be suppressed");
        LOG_WARN ("test", "WARN visible");
        LOG_ERROR("test", "ERROR visible");
    } catch (...) {
        no_throw = false;
    }
    test::check("LOG_* macros do not throw", no_throw);

    // Restore default level
    logger.set_level(helios::LogLevel::INFO);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "═══════════════════════════════════════════\n";
    std::cout << "  Helios Test Suite — Phase 0\n";
    std::cout << "═══════════════════════════════════════════\n";

    test_version();
    test_config_defaults();
    test_logger_level_filtering();

    return test::summarise();
}

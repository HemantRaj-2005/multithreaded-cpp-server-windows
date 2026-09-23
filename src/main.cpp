// ─────────────────────────────────────────────────────────────────────────────
//  src/main.cpp
//  Helios HTTP Server — Entry Point
//
//  Phase 0: Project Foundation
//  No networking in this phase. This file sets up the configuration and
//  logging subsystems and verifies they work correctly.
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/config.hpp"
#include "helios/logger.hpp"
#include "helios/version.hpp"

#include <cstdlib>   // EXIT_SUCCESS
#include <iostream>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

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

// Map the log-level string from config to the enum value.
// Unknown strings default to INFO — fail-safe, not fail-loud.
helios::LogLevel parse_log_level(const std::string& s) {
    if (s == "TRACE") return helios::LogLevel::TRACE;
    if (s == "DEBUG") return helios::LogLevel::DEBUG;
    if (s == "INFO")  return helios::LogLevel::INFO;
    if (s == "WARN")  return helios::LogLevel::WARN;
    if (s == "ERROR") return helios::LogLevel::ERROR;
    if (s == "FATAL") return helios::LogLevel::FATAL;
    return helios::LogLevel::INFO; // safe default
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
//  main()
//
//  Startup sequence:
//    1. Print banner  (always, before logging is ready)
//    2. Load config
//    3. Initialise logger from config
//    4. Log startup summary
//    5. (Phase 1+) Start TCP server
//    6. (Phase 1+) Enter accept/event loop
//    7. Graceful shutdown
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {

    // ── 1. Banner ─────────────────────────────────────────────────────────
    print_banner();

    // ── 2. Config ─────────────────────────────────────────────────────────
    // Default config path; override by passing a path as the first argument.
    //   .\helios.exe                      → reads config/server.conf
    //   .\helios.exe path\to\my.conf      → reads that file
    const char* config_path = (argc >= 2) ? argv[1] : "config/server.conf";

    auto& cfg = helios::Config::instance();
    const bool cfg_loaded = cfg.load(config_path);

    // ── 3. Logger ─────────────────────────────────────────────────────────
    const std::string level_str = cfg.get_string("logging", "level", "INFO");
    helios::Logger::instance().set_level(parse_log_level(level_str));

    // ── 4. Startup summary ────────────────────────────────────────────────
    if (cfg_loaded) {
        LOG_INFO("main", "Config loaded from: " + std::string(config_path));
    } else {
        LOG_WARN("main", std::string("Config file not found at '") + config_path +
                         "' — using built-in defaults for all settings");
    }

    const std::string sep(52, '=');
    LOG_INFO("main", sep);
    LOG_INFO("main", "  Server name  : Helios HTTP Server");
    LOG_INFO("main", "  Version      : " + std::string(helios::VERSION_STRING));
    LOG_INFO("main", "  Phase        : " + std::to_string(helios::PHASE)
                                         + " — " + helios::PHASE_NAME);
    LOG_INFO("main", "  Networking   : Not yet implemented (Phase 1)");
    LOG_INFO("main", "  Log level    : " + level_str);
    LOG_INFO("main", sep);

    // Demonstrate log levels so the user can verify the logger works.
    LOG_DEBUG("main", "DEBUG messages are visible (log level is DEBUG or lower)");
    LOG_TRACE("main", "TRACE messages are visible (log level is TRACE)");

    // ── 5. Phase 0 complete ───────────────────────────────────────────────
    LOG_INFO("main", "Phase 0 complete. Foundation is solid.");
    LOG_INFO("main", "Next: implement Phase 1 — Basic Single-Threaded TCP Server.");

    return EXIT_SUCCESS;
}

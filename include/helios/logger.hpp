#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/logger.hpp
//  Thread-safe, header-only logger for Helios.
//
//  Design decisions:
//    • Header-only — no build step needed, just #include
//    • Singleton — one logger per process, accessible everywhere
//    • std::mutex-protected — safe to call from multiple threads (Phase 3+)
//    • Six log levels: TRACE < DEBUG < INFO < WARN < ERR < FATAL_
//    • Output goes to std::cerr (unbuffered on most platforms)
//    • Format: [YYYY-MM-DD HH:MM:SS] [LEVEL] [component] message
//
//  Windows macro collision note:
//    <wingdi.h> (pulled in by <winsock2.h> / <windows.h>) defines:
//        #define ERROR  0
//    If we named our enum member "ERROR", the preprocessor would rewrite
//    "ERROR = 4" as "0 = 4" — a syntax error.  The same risk exists for any
//    Windows macro name we might choose as an enum value.
//
//    Solution: enum values that collide with Windows macros are renamed with
//    a trailing underscore: ERR, FATAL_.  The user-facing macros LOG_ERROR
//    and LOG_FATAL are unchanged — only the internal enum spelling differs.
//    This is the approach used by Abseil's absl::LogSeverity.
//
//  Usage:
//    #include "helios/logger.hpp"
//
//    LOG_INFO ("router",  "GET /health 200 1ms");
//    LOG_ERROR ("socket", "bind() failed: " + std::to_string(err));
//    LOG_DEBUG ("parser", "Parsed " + std::to_string(n) + " headers");
//
//  Runtime level change:
//    helios::Logger::instance().set_level(helios::LogLevel::ERR);
// ─────────────────────────────────────────────────────────────────────────────

#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>

namespace helios {

// ─────────────────────────────────────────────────────────────────────────────
//  LogLevel
//
//  Naming:
//    ERR   instead of ERROR — <wingdi.h> #defines ERROR as 0
//    FATAL_ instead of FATAL — defensive; FATAL is not currently a Windows
//                              macro but has historically been defined by
//                              some Windows SDK versions.
//
//  The ordering is significant: levels are compared numerically.
//  A logger at level INFO suppresses all levels with a smaller integer value
//  (TRACE, DEBUG).
// ─────────────────────────────────────────────────────────────────────────────
enum class LogLevel : int {
    TRACE  = 0,  // Fine-grained execution tracing
    DEBUG  = 1,  // Diagnostic information useful during development
    INFO   = 2,  // Normal operational messages (default)
    WARN   = 3,  // Unexpected situation, but execution continues
    ERR    = 4,  // Error that affects a single request/operation  (not ERROR — see above)
    FATAL_ = 5,  // Unrecoverable error — process must exit        (not FATAL — defensive)
    OFF    = 6   // Disable all logging
};

// ─────────────────────────────────────────────────────────────────────────────
//  Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace detail {

inline std::string_view level_to_str(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::TRACE:  return "TRACE";
        case LogLevel::DEBUG:  return "DEBUG";
        case LogLevel::INFO:   return "INFO ";
        case LogLevel::WARN:   return "WARN ";
        case LogLevel::ERR:    return "ERROR";   // display string stays "ERROR"
        case LogLevel::FATAL_: return "FATAL";   // display string stays "FATAL"
        default:               return "?????";
    }
}

// Returns the current local time formatted as "YYYY-MM-DD HH:MM:SS".
//
// Thread-safety:
//   - On Windows (MSVC + MinGW): localtime_s(&tm, &t) — thread-safe.
//     Both compilers share the same signature on Windows.
//   - On POSIX (Linux/macOS): localtime_r(&t, &tm) — note reversed args.
//   - Fallback: non-thread-safe localtime(). Safe here because the caller
//     already holds the logger mutex.
inline std::string current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf{};
#if defined(_WIN32)
    ::localtime_s(&tm_buf, &t);
#elif defined(__unix__) || defined(__APPLE__)
    ::localtime_r(&t, &tm_buf);
#else
    const std::tm* tmp = std::localtime(&t);
    if (tmp) tm_buf = *tmp;
#endif

    char buf[20]; // "YYYY-MM-DD HH:MM:SS\0"
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
//  Logger
//  Singleton logger. Thread-safe at the cost of one mutex lock per log call.
//
//  Thread-safety rationale:
//    A single std::mutex protects both the level check and the write to cerr.
//    This is sufficient for Phase 0-2. In Phase 6 (Observability) we can
//    introduce a lock-free ring buffer + dedicated writer thread if profiling
//    shows the mutex is a bottleneck.
// ─────────────────────────────────────────────────────────────────────────────
class Logger {
public:
    // Meyer's singleton — constructed exactly once, destroyed on process exit.
    static Logger& instance() noexcept {
        static Logger inst;
        return inst;
    }

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&)                 = delete;
    Logger& operator=(Logger&&)      = delete;

    void set_level(LogLevel level) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = level;
    }

    LogLevel level() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return min_level_;
    }

    // component: short subsystem identifier, e.g. "router", "http"
    // message  : pre-formatted log string
    void log(LogLevel lvl, std::string_view component, std::string_view message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lvl < min_level_) return;

        std::cerr
            << '[' << detail::current_timestamp() << ']'
            << " [" << detail::level_to_str(lvl) << ']'
            << " [" << component << "] "
            << message
            << '\n';
    }

private:
    Logger()  = default;
    ~Logger() = default;

    mutable std::mutex mutex_;
    LogLevel           min_level_{LogLevel::INFO};
};

} // namespace helios

// ─────────────────────────────────────────────────────────────────────────────
//  Convenience macros
//
//  Why macros instead of inline functions?
//    Macros allow the message expression to be skipped entirely when the
//    level is filtered out.  An inline function always evaluates its
//    arguments (including string concatenation) even if nothing is logged.
//
//  The do { ... } while(false) idiom makes the macros safe in all contexts.
//
//  LOG_ERROR maps to LogLevel::ERR     (not ERROR — Windows macro collision).
//  LOG_FATAL maps to LogLevel::FATAL_  (not FATAL — defensive).
//  The caller always writes LOG_ERROR / LOG_FATAL; the internal name is an
//  implementation detail.
// ─────────────────────────────────────────────────────────────────────────────
#define HELIOS_LOG(lvl, component, message)                                     \
    do {                                                                         \
        if (static_cast<int>((lvl)) >=                                          \
            static_cast<int>(::helios::Logger::instance().level())) {           \
            ::helios::Logger::instance().log((lvl), (component), (message));    \
        }                                                                        \
    } while (false)

// clang-format off
#define LOG_TRACE(comp, msg) HELIOS_LOG(::helios::LogLevel::TRACE,  (comp), (msg))
#define LOG_DEBUG(comp, msg) HELIOS_LOG(::helios::LogLevel::DEBUG,  (comp), (msg))
#define LOG_INFO(comp, msg)  HELIOS_LOG(::helios::LogLevel::INFO,   (comp), (msg))
#define LOG_WARN(comp, msg)  HELIOS_LOG(::helios::LogLevel::WARN,   (comp), (msg))
#define LOG_ERROR(comp, msg) HELIOS_LOG(::helios::LogLevel::ERR,    (comp), (msg))
#define LOG_FATAL(comp, msg) HELIOS_LOG(::helios::LogLevel::FATAL_, (comp), (msg))
// clang-format on

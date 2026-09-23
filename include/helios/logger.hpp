#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/logger.hpp
//  Thread-safe, header-only logger for Helios.
//
//  Design decisions:
//    • Header-only — no build step needed, just #include
//    • Singleton — one logger per process, accessible everywhere
//    • std::mutex-protected — safe to call from multiple threads (Phase 3+)
//    • Six log levels: TRACE < DEBUG < INFO < WARN < ERROR < FATAL
//    • Output goes to std::cerr (unbuffered on most platforms)
//    • Format: [YYYY-MM-DD HH:MM:SS] [LEVEL] [component] message
//
//  Usage:
//    #include "helios/logger.hpp"
//
//    LOG_INFO("router",  "GET /health 200 1ms");
//    LOG_ERROR("socket", "bind() failed: " + std::to_string(err));
//    LOG_DEBUG("parser", "Parsed " + std::to_string(n) + " headers");
//
//  Runtime level change:
//    helios::Logger::instance().set_level(helios::LogLevel::DEBUG);
// ─────────────────────────────────────────────────────────────────────────────

#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace helios {

// ─────────────────────────────────────────────────────────────────────────────
//  LogLevel
//  Ordered — a logger configured at INFO will suppress TRACE and DEBUG.
// ─────────────────────────────────────────────────────────────────────────────
enum class LogLevel : int {
    TRACE = 0,  // Fine-grained execution tracing
    DEBUG = 1,  // Diagnostic information useful during development
    INFO  = 2,  // Normal operational messages (default)
    WARN  = 3,  // Unexpected situation, but execution continues
    ERROR = 4,  // Error that affects a single request/operation
    FATAL = 5,  // Unrecoverable error — process must exit
    OFF   = 6   // Disable all logging
};

// ─────────────────────────────────────────────────────────────────────────────
//  Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace detail {

inline std::string_view level_to_str(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:              return "?????";
    }
}

// Returns the current local time formatted as "YYYY-MM-DD HH:MM:SS".
//
// Thread-safety:
//   - On Windows (both MSVC and MinGW): localtime_s(&tm, &t) — thread-safe,
//     same signature on both compilers.
//   - On POSIX (Linux/macOS): localtime_r(&t, &tm) — thread-safe POSIX API.
//   - Fallback: plain localtime() — not thread-safe, but we only reach this
//     path on exotic platforms; the caller already holds the logger mutex
//     so it is safe in practice.
inline std::string current_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf{};
#if defined(_WIN32)
    // Works on both MSVC and MinGW-GCC on Windows.
    // Signature: errno_t localtime_s(struct tm*, const time_t*)
    ::localtime_s(&tm_buf, &t);
#elif defined(__unix__) || defined(__APPLE__)
    // POSIX: localtime_r(const time_t*, struct tm*)  — note reversed args!
    ::localtime_r(&t, &tm_buf);
#else
    // Non-thread-safe fallback for unknown platforms.
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
    // Meyer's singleton — guaranteed to be constructed exactly once,
    // and destroyed when the program exits.
    static Logger& instance() noexcept {
        static Logger inst;
        return inst;
    }

    // Non-copyable, non-movable — there should be exactly one Logger.
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&)                 = delete;
    Logger& operator=(Logger&&)      = delete;

    // ── Configuration ─────────────────────────────────────────────────────
    void set_level(LogLevel level) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        min_level_ = level;
    }

    LogLevel level() const noexcept {
        // Reading an int-sized atomic value — relaxed read is fine here.
        // Using mutex for simplicity; can upgrade to std::atomic<LogLevel>
        // if profiling shows contention.
        std::lock_guard<std::mutex> lock(mutex_);
        return min_level_;
    }

    // ── Primary log method ────────────────────────────────────────────────
    //  component: short identifier of the calling subsystem, e.g. "router"
    //  message  : pre-formatted log message string
    void log(LogLevel lvl, std::string_view component, std::string_view message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lvl < min_level_) return;

        // Format: [YYYY-MM-DD HH:MM:SS] [LEVEL] [component] message
        std::cerr
            << '[' << detail::current_timestamp() << ']'
            << " [" << detail::level_to_str(lvl) << ']'
            << " [" << component << "] "
            << message
            << '\n';
    }

private:
    Logger() = default;
    ~Logger() = default;

    mutable std::mutex mutex_;
    LogLevel           min_level_{LogLevel::INFO};
};

} // namespace helios

// ─────────────────────────────────────────────────────────────────────────────
//  Convenience macros
//
//  Why macros instead of inline functions?
//    Macros allow the message expression to be skipped entirely at compile time
//    when the level is below the threshold. A function would always evaluate
//    the arguments (including string concatenation) even if the message is
//    never written.
//
//  The do { ... } while(false) idiom makes the macro safe in all contexts,
//  including single-line if statements without braces.
// ─────────────────────────────────────────────────────────────────────────────
#define HELIOS_LOG(lvl, component, message)                                    \
    do {                                                                        \
        if (static_cast<int>((lvl)) >=                                         \
            static_cast<int>(::helios::Logger::instance().level())) {          \
            ::helios::Logger::instance().log((lvl), (component), (message));   \
        }                                                                       \
    } while (false)

// clang-format off
#define LOG_TRACE(comp, msg) HELIOS_LOG(::helios::LogLevel::TRACE, (comp), (msg))
#define LOG_DEBUG(comp, msg) HELIOS_LOG(::helios::LogLevel::DEBUG, (comp), (msg))
#define LOG_INFO(comp, msg)  HELIOS_LOG(::helios::LogLevel::INFO,  (comp), (msg))
#define LOG_WARN(comp, msg)  HELIOS_LOG(::helios::LogLevel::WARN,  (comp), (msg))
#define LOG_ERROR(comp, msg) HELIOS_LOG(::helios::LogLevel::ERROR, (comp), (msg))
#define LOG_FATAL(comp, msg) HELIOS_LOG(::helios::LogLevel::FATAL, (comp), (msg))
// clang-format on

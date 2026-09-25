#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/net/winsock_init.hpp
//  RAII guard for Winsock initialisation (WSAStartup / WSACleanup).
//
//  Design rationale:
//    Every Winsock application must call WSAStartup() before the first socket
//    call and WSACleanup() before process exit.  Forgetting WSACleanup() leaks
//    OS-level resources and can cause issues with certain network drivers.
//    A RAII guard makes this automatic regardless of how main() exits.
//
//  Usage (in main(), before any socket call):
//    helios::net::WinsockGuard winsock;
//    if (!winsock.ok()) {
//        LOG_FATAL("main", "Winsock init failed: " + winsock.error_message());
//        return EXIT_FAILURE;
//    }
//    // ... socket calls ...
//    // WinsockGuard destructor → WSACleanup() called automatically
//
//  One WinsockGuard per process is sufficient and required.
// ─────────────────────────────────────────────────────────────────────────────

// winsock2.h MUST be included before windows.h.
// If windows.h is included first it pulls in the older winsock.h, causing
// redefinition errors.  WIN32_LEAN_AND_MEAN suppresses windows.h's own
// winsock.h pull-in.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>

namespace helios::net {

// ─────────────────────────────────────────────────────────────────────────────
//  wsa_error_string
//  Convert a Winsock error code to a human-readable string.
//
//  Pass -1 (default) to use WSAGetLastError() automatically.
//
//  Example:
//    if (::bind(sock, ...) == SOCKET_ERROR) {
//        LOG_ERROR("net", "bind() failed: " + helios::net::wsa_error_string());
//    }
// ─────────────────────────────────────────────────────────────────────────────
inline std::string wsa_error_string(int code = -1) {
    if (code == -1) code = ::WSAGetLastError();

    // FormatMessageA retrieves the system message for this error code.
    // FORMAT_MESSAGE_ALLOCATE_BUFFER: Windows allocates the output buffer;
    // we must call LocalFree() on it.
    LPSTR msg_buf = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM     |
                        FORMAT_MESSAGE_IGNORE_INSERTS;

    const DWORD len = ::FormatMessageA(
        flags, nullptr,
        static_cast<DWORD>(code),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&msg_buf),
        0, nullptr);

    std::string result;
    if (len > 0 && msg_buf) {
        result = msg_buf;
        // FormatMessage often appends "\r\n" — strip it.
        while (!result.empty() &&
               (result.back() == '\r' || result.back() == '\n')) {
            result.pop_back();
        }
    } else {
        result = "Unknown Winsock error";
    }
    result += " (WSA error " + std::to_string(code) + ')';

    if (msg_buf) ::LocalFree(msg_buf);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  WinsockGuard
//  RAII wrapper: WSAStartup on construction, WSACleanup on destruction.
// ─────────────────────────────────────────────────────────────────────────────
class WinsockGuard {
public:
    WinsockGuard() {
        WSADATA data{};
        // MAKEWORD(2, 2) requests Winsock version 2.2 — the current and
        // final version of the Winsock API.  The OS may negotiate a lower
        // compatible version; for our purposes 2.2 is always available on
        // Windows XP and later.
        const int rc = ::WSAStartup(MAKEWORD(2, 2), &data);
        if (rc == 0) {
            initialized_ = true;
        } else {
            // WSAStartup failed — WSAGetLastError() is NOT valid here;
            // the return value of WSAStartup IS the error code.
            error_ = wsa_error_string(rc);
        }
    }

    ~WinsockGuard() {
        if (initialized_) {
            ::WSACleanup();
        }
    }

    // Non-copyable, non-movable.
    // Only one WinsockGuard should exist per process.
    WinsockGuard(const WinsockGuard&)            = delete;
    WinsockGuard& operator=(const WinsockGuard&) = delete;
    WinsockGuard(WinsockGuard&&)                 = delete;
    WinsockGuard& operator=(WinsockGuard&&)      = delete;

    bool        ok()            const noexcept { return initialized_; }
    std::string error_message() const noexcept { return error_; }

private:
    bool        initialized_{false};
    std::string error_;
};

} // namespace helios::net

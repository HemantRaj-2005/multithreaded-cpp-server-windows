#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/net/tcp_connection.hpp
//  RAII wrapper for a single accepted TCP client socket.
//
//  The Problem with Raw Sockets:
//    SOCKET is typedef'd as UINT_PTR on Windows — just an integer.  The OS
//    tracks open sockets as kernel objects.  If you forget to call
//    closesocket(), the descriptor leaks until the process exits.  Under load
//    (thousands of requests), this depletes the process's file descriptor
//    limit and the server starts failing accept() with WSAEMFILE.
//
//  The Solution — RAII:
//    TcpConnection holds the SOCKET and closes it in the destructor.
//    Correctness is guaranteed regardless of how the function exits (normal
//    return, exception, early return on error).
//
//  Ownership model:
//    - Non-copyable (one owner at a time — mirrors the OS resource model).
//    - Movable: a TcpConnection can be handed off to a handler or worker
//      thread by std::move().  After the move, the source object holds
//      INVALID_SOCKET and its destructor is a no-op.
//
//  Phase 3 note:
//    When we introduce worker threads, the connection will be std::move()'d
//    into the thread pool task.  The TcpConnection API does not change.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <string_view>

namespace helios::net {

class TcpConnection {
public:
    // Construct from an already-accepted socket and the peer's address.
    // Called internally by TcpServer::run() after accept() succeeds.
    explicit TcpConnection(SOCKET fd, const sockaddr_in& peer_addr) noexcept;

    // Closes the socket (shutdown + closesocket) if still valid.
    ~TcpConnection();

    // ── Ownership ─────────────────────────────────────────────────────────
    TcpConnection(const TcpConnection&)            = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    TcpConnection(TcpConnection&&) noexcept;
    TcpConnection& operator=(TcpConnection&&) noexcept;

    // ── I/O primitives ────────────────────────────────────────────────────

    // Read up to `len` bytes into `buf`.
    // Returns: > 0  bytes actually read
    //            0  peer closed the connection (clean EOF)
    //           -1  error (WSAGetLastError() for details; WSAETIMEDOUT after 5s)
    int read(char* buf, int len) noexcept;

    // Send exactly `len` bytes from `data`.
    // Internally loops over send() to handle partial sends — the OS may
    // accept fewer bytes than requested when the kernel send buffer is full.
    // Returns true on success, false on disconnection or error.
    bool write(const char* data, int len) noexcept;
    bool write(std::string_view data)     noexcept;

    // ── Higher-level helpers ──────────────────────────────────────────────

    // Reads HTTP request headers until the blank line (\r\n\r\n) that
    // terminates them, or until `max_bytes` are accumulated, whichever comes
    // first.  Does NOT consume the request body (Phase 1 serves GET only).
    // The accepted socket has a 5-second receive timeout set in the
    // constructor to prevent slow-client DoS.
    std::string read_request(int max_bytes = 8192);

    // ── Status ────────────────────────────────────────────────────────────
    bool        is_valid()     const noexcept { return fd_ != INVALID_SOCKET; }
    SOCKET      fd()           const noexcept { return fd_; }
    // Returns "IP:port", e.g. "192.168.1.5:54321"
    std::string peer_address() const;

private:
    void do_close() noexcept;

    SOCKET      fd_{INVALID_SOCKET};
    sockaddr_in peer_addr_{};
};

} // namespace helios::net

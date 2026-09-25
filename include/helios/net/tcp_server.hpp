#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/net/tcp_server.hpp
//  Single-threaded blocking TCP server (Phase 1).
//
//  The TCP server lifecycle:
//
//    socket()      — Ask the OS for a socket file descriptor.
//    setsockopt()  — Configure socket options (SO_REUSEADDR).
//    bind()        — Attach to a local IP:port.
//    listen()      — Start accepting connection requests.
//    [accept loop]
//      select()    — Wait up to 500ms for a new connection.
//      accept()    — Dequeue one pending connection → new client socket.
//      handler()   — Process the connection synchronously.
//    closesocket() — Release the listening socket.
//
//  Why select() instead of blocking accept()?
//    A bare accept() blocks indefinitely with no way to interrupt it from
//    another context.  select() with a 500ms timeout lets the loop check
//    `running_` periodically, giving stop() a guaranteed ≤500ms response.
//
//  Phase 3 upgrade path:
//    The TcpServer API is stable. In Phase 3 we replace:
//      handler(std::move(conn));   // synchronous
//    with:
//      thread_pool_.submit(std::move(conn));   // asynchronous
//    No other code changes required.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <functional>
#include <string>
#include "helios/net/tcp_connection.hpp"

namespace helios::net {

class TcpServer {
public:
    // Type of the per-connection handler.
    // Phase 1: called synchronously on the accept thread.
    // Phase 3: submitted to the thread pool.
    using ConnectionHandler = std::function<void(TcpConnection)>;

    // `port`    — TCP port to listen on.
    // `backlog` — Maximum number of pending connections in the kernel's
    //             accept queue.  Connections arriving when the queue is full
    //             are refused by the OS with a TCP RST.
    explicit TcpServer(int port, int backlog = 128) noexcept;

    // Closes the listening socket.
    ~TcpServer();

    // Non-copyable, non-movable.
    TcpServer(const TcpServer&)            = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&&)                 = delete;
    TcpServer& operator=(TcpServer&&)      = delete;

    // ── Lifecycle ──────────────────────────────────────────────────────────

    // socket() → setsockopt() → bind() → listen().
    // Logs each failure with details.
    // Returns false on any error; true on success.
    bool start();

    // Blocking accept loop.  Returns only after stop() is called.
    // `handler` is invoked once per accepted connection.
    void run(ConnectionHandler handler);

    // Signal the accept loop to stop.
    // Thread-safe: uses std::atomic; safe to call from a signal handler.
    void stop() noexcept { running_.store(false); }

    // ── Status ─────────────────────────────────────────────────────────────
    bool is_running() const noexcept { return running_.load(); }
    int  port()       const noexcept { return port_; }

private:
    void close_server_socket() noexcept;

    SOCKET            server_fd_{INVALID_SOCKET};
    int               port_;
    int               backlog_;
    std::atomic<bool> running_{false};
};

} // namespace helios::net

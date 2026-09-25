// ─────────────────────────────────────────────────────────────────────────────
//  src/net/tcp_server.cpp
//  TcpServer implementation.
//
//  TCP Socket Lifecycle — a map of what happens in this file:
//
//   ┌─────────────────────────────────────────────────────────────────┐
//   │  socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)                      │
//   │    → OS allocates a socket object; returns a handle (fd)        │
//   ├─────────────────────────────────────────────────────────────────┤
//   │  setsockopt(SO_REUSEADDR)                                       │
//   │    → Allow re-binding to port even if it is in TIME_WAIT state  │
//   ├─────────────────────────────────────────────────────────────────┤
//   │  bind(fd, {INADDR_ANY, port})                                   │
//   │    → Associates the socket with a local IP:port                 │
//   │    → INADDR_ANY = listen on all network interfaces              │
//   ├─────────────────────────────────────────────────────────────────┤
//   │  listen(fd, backlog)                                            │
//   │    → OS begins accepting connection requests into a queue       │
//   │    → Connections beyond `backlog` are dropped (RST sent)        │
//   ├─────────────────────────────────────────────────────────────────┤
//   │  [accept loop]                                                  │
//   │    select(fd, timeout=500ms)  → wait for incoming connection    │
//   │    accept(fd, &client_addr)   → dequeue one → new client socket │
//   │    handler(TcpConnection{client_fd})                            │
//   ├─────────────────────────────────────────────────────────────────┤
//   │  closesocket(fd)                                                │
//   │    → Releases the listening socket                              │
//   └─────────────────────────────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/net/tcp_server.hpp"
#include "helios/net/winsock_init.hpp"   // wsa_error_string
#include "helios/logger.hpp"

namespace helios::net {

// ── Constructor / Destructor ──────────────────────────────────────────────────

TcpServer::TcpServer(int port, int backlog) noexcept
    : port_{port}, backlog_{backlog}
{}

TcpServer::~TcpServer() {
    close_server_socket();
}

// ── start() ───────────────────────────────────────────────────────────────────

bool TcpServer::start() {
    // ── 1. socket() ───────────────────────────────────────────────────────
    // Arguments:
    //   AF_INET     = IPv4 address family
    //   SOCK_STREAM = TCP (reliable, ordered, connection-oriented byte stream)
    //   IPPROTO_TCP = explicitly select TCP (redundant with SOCK_STREAM but clear)
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd_ == INVALID_SOCKET) {
        LOG_ERROR("TcpServer", "socket() failed: " + wsa_error_string());
        return false;
    }
    LOG_DEBUG("TcpServer", "Socket created (fd=" +
              std::to_string(static_cast<int>(server_fd_)) + ')');

    // ── 2. setsockopt(SO_REUSEADDR) ───────────────────────────────────────
    // TCP TIME_WAIT explained:
    //   After the server closes a connection, the OS keeps the local port in
    //   TIME_WAIT state for 2×MSL (Maximum Segment Lifetime ≈ 1-4 minutes).
    //   This ensures any delayed packets from the old connection are discarded
    //   rather than being misinterpreted by a new connection on the same port.
    //
    //   Problem: if the server crashes and restarts, bind() fails with
    //   WSAEADDRINUSE because the port is still in TIME_WAIT.
    //
    //   SO_REUSEADDR: tell the OS to allow binding to the port even if it is
    //   in TIME_WAIT.  Standard practice for all server sockets.
    const int opt = 1;
    if (::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&opt),
                     static_cast<int>(sizeof(opt))) == SOCKET_ERROR) {
        // Non-fatal: log a warning and continue.  Rapid restarts may
        // temporarily fail with WSAEADDRINUSE but the server will work.
        LOG_WARN("TcpServer",
                 "setsockopt(SO_REUSEADDR) failed: " + wsa_error_string() +
                 " — rapid restarts may temporarily fail with EADDRINUSE");
    }

    // ── 3. bind() ────────────────────────────────────────────────────────
    // sockaddr_in describes the local address to bind to.
    //   sin_family = AF_INET (must match the socket address family)
    //   sin_port   = port in network byte order (big-endian)
    //                htons() = host-to-network-short: converts little-endian
    //                x86 integers to big-endian for the network protocol
    //   sin_addr   = INADDR_ANY (0.0.0.0) = bind to all interfaces
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = ::htons(static_cast<u_short>(port_));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server_fd_,
               reinterpret_cast<const sockaddr*>(&addr),
               static_cast<int>(sizeof(addr))) == SOCKET_ERROR) {
        LOG_ERROR("TcpServer",
                  "bind() on port " + std::to_string(port_) +
                  " failed: " + wsa_error_string());
        close_server_socket();
        return false;
    }
    LOG_DEBUG("TcpServer", "Bound to port " + std::to_string(port_));

    // ── 4. listen() ──────────────────────────────────────────────────────
    // After listen(), the OS accepts incoming TCP SYN packets and completes
    // the 3-way handshake on our behalf.  Fully established connections wait
    // in the kernel's accept queue.  Our accept() dequeues them one by one.
    //
    // backlog_ = max connections in the accept queue.  When the queue is
    // full, new SYN packets are silently dropped (client retries) or RST.
    // A typical value is 128 or SOMAXCONN.
    if (::listen(server_fd_, backlog_) == SOCKET_ERROR) {
        LOG_ERROR("TcpServer", "listen() failed: " + wsa_error_string());
        close_server_socket();
        return false;
    }
    LOG_INFO("TcpServer",
             "Listening on 0.0.0.0:" + std::to_string(port_) +
             " (backlog=" + std::to_string(backlog_) + ')');
    return true;
}

// ── run() ─────────────────────────────────────────────────────────────────────

void TcpServer::run(ConnectionHandler handler) {
    running_.store(true);
    LOG_INFO("TcpServer", "Accept loop started  [Phase 1 — single-threaded]");

    while (running_.load()) {

        // ── select() with 500ms timeout ───────────────────────────────────
        //
        // What does select() do?
        //   It watches a set of socket descriptors and returns when at least
        //   one of them is ready for I/O (or when the timeout expires).
        //
        // fd_set is a bitmask of socket descriptors to watch.
        //   FD_ZERO clears it.
        //   FD_SET adds server_fd_ to the "read" watch set.
        //
        // A listening socket is "readable" when a client connection is waiting
        // to be accept()-ed.
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(server_fd_, &read_set);

        // timeval { tv_sec, tv_usec }
        // 500'000 microseconds = 500 milliseconds
        // This caps the maximum latency between stop() being called and the
        // accept loop actually terminating.
        timeval timeout{0, 500'000};

        // select() arguments:
        //   nfds     = highest fd + 1 (ignored on Windows; required for POSIX)
        //   readfds  = sockets to watch for readability
        //   writefds = sockets to watch for writability (nullptr = don't watch)
        //   exceptfds= sockets to watch for exceptions  (nullptr = don't watch)
        //   timeout  = maximum wait time
        const int ready = ::select(
            static_cast<int>(server_fd_) + 1,
            &read_set, nullptr, nullptr, &timeout);

        if (ready == SOCKET_ERROR) {
            if (running_.load()) {
                LOG_ERROR("TcpServer", "select() failed: " + wsa_error_string());
            }
            break;
        }

        // Timeout: no connection arrived in 500ms.  Loop back to re-check running_.
        if (ready == 0) continue;

        // ── accept() ─────────────────────────────────────────────────────
        // accept() dequeues ONE pending connection from the kernel's queue.
        // It returns a NEW socket descriptor for that specific client.
        // The server_fd_ (listening socket) remains open and continues
        // accepting further connections.
        //
        // client_addr is filled with the peer's IP address and port.
        sockaddr_in client_addr{};
        int         addr_len = static_cast<int>(sizeof(client_addr));

        const SOCKET client_fd = ::accept(
            server_fd_,
            reinterpret_cast<sockaddr*>(&client_addr),
            &addr_len);

        if (client_fd == INVALID_SOCKET) {
            if (running_.load()) {
                LOG_WARN("TcpServer", "accept() failed: " + wsa_error_string());
            }
            continue; // non-fatal: try next iteration
        }

        // Wrap the client socket in a RAII TcpConnection.
        // If handler() throws, conn's destructor still closes the socket.
        TcpConnection conn{client_fd, client_addr};
        LOG_DEBUG("TcpServer", "Connection from " + conn.peer_address());

        // ── Invoke handler ────────────────────────────────────────────────
        // Phase 1: SYNCHRONOUS — the accept loop is blocked while the handler
        // processes this request.  The server cannot accept a second client
        // until the handler returns.
        //
        // This is the main limitation of the single-threaded model.
        // We fix it in Phase 3 by moving `std::move(conn)` into a thread pool.
        try {
            handler(std::move(conn));
        } catch (const std::exception& ex) {
            LOG_ERROR("TcpServer",
                      std::string("Handler threw exception: ") + ex.what());
        } catch (...) {
            LOG_ERROR("TcpServer", "Handler threw unknown exception");
        }
        // conn is now in a moved-from state (fd_ == INVALID_SOCKET).
        // Its destructor is a no-op.
    }

    LOG_INFO("TcpServer", "Accept loop exited");
}

// ── Private helpers ───────────────────────────────────────────────────────────

void TcpServer::close_server_socket() noexcept {
    if (server_fd_ != INVALID_SOCKET) {
        ::closesocket(server_fd_);
        server_fd_ = INVALID_SOCKET;
    }
}

} // namespace helios::net

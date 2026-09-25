// ─────────────────────────────────────────────────────────────────────────────
//  src/net/tcp_connection.cpp
//  TcpConnection implementation.
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/net/tcp_connection.hpp"
#include "helios/net/winsock_init.hpp"   // wsa_error_string
#include "helios/logger.hpp"

#include <utility>  // std::exchange

namespace helios::net {

// ── Constructor ───────────────────────────────────────────────────────────────

TcpConnection::TcpConnection(SOCKET fd, const sockaddr_in& peer_addr) noexcept
    : fd_{fd}, peer_addr_{peer_addr}
{
    if (fd_ == INVALID_SOCKET) return;

    // Set a receive timeout of 5 seconds on this client socket.
    //
    // Why is this necessary?
    //   A malicious or slow client can connect and then never send any data.
    //   Without a timeout, recv() would block forever, tying up the server's
    //   single thread (Phase 1) or a worker thread (Phase 3+).
    //   After 5 seconds of inactivity, recv() returns WSAETIMEDOUT (-1) and
    //   read_request() breaks out of its loop.
    //
    // Phase 5 upgrade: read timeout from Config("server.request_timeout").
    const DWORD timeout_ms = 5000;
    if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeout_ms),
                     static_cast<int>(sizeof(timeout_ms))) == SOCKET_ERROR) {
        LOG_WARN("TcpConnection",
                 "setsockopt(SO_RCVTIMEO) failed: " + wsa_error_string() +
                 " — no receive timeout on this connection");
    }
}

// ── Destructor ────────────────────────────────────────────────────────────────

TcpConnection::~TcpConnection() {
    do_close();
}

// ── Move semantics ────────────────────────────────────────────────────────────
//
// Why support move but not copy?
//   A socket is a unique OS resource — there can only be one valid handle to
//   it.  Copying would create two objects that both believe they own the same
//   kernel resource, leading to a double-close when both destructors run.
//
//   Move, on the other hand, transfers ownership: the source object's fd_ is
//   set to INVALID_SOCKET, making its destructor a no-op.

TcpConnection::TcpConnection(TcpConnection&& other) noexcept
    : fd_       {std::exchange(other.fd_, INVALID_SOCKET)}
    , peer_addr_{other.peer_addr_}
{}

TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept {
    if (this != &other) {
        do_close();                               // release current resource
        fd_        = std::exchange(other.fd_, INVALID_SOCKET);
        peer_addr_ = other.peer_addr_;
    }
    return *this;
}

// ── I/O primitives ────────────────────────────────────────────────────────────

int TcpConnection::read(char* buf, int len) noexcept {
    // recv() returns:
    //   > 0  : number of bytes received
    //     0  : connection closed by remote peer (FIN received)
    //    -1  : SOCKET_ERROR — call WSAGetLastError() for the code
    //          Common codes: WSAETIMEDOUT (timeout), WSAECONNRESET (RST from peer)
    return ::recv(fd_, buf, len, 0);
}

bool TcpConnection::write(const char* data, int len) noexcept {
    // send() may transmit fewer bytes than requested — this is called a
    // "partial send".  It occurs when the kernel's TCP send buffer is full
    // (the remote side is reading slowly).  The OS copies only what fits
    // and returns that count.
    //
    // We MUST loop until all bytes are sent.  Failing to do so means the
    // client receives a truncated response and cannot parse the HTTP message.
    int total_sent = 0;
    while (total_sent < len) {
        const int sent = ::send(fd_, data + total_sent, len - total_sent, 0);
        if (sent == SOCKET_ERROR) {
            LOG_DEBUG("TcpConnection",
                      "send() failed after " + std::to_string(total_sent) +
                      " bytes: " + wsa_error_string());
            return false;
        }
        total_sent += sent;
    }
    return true;
}

bool TcpConnection::write(std::string_view data) noexcept {
    return write(data.data(), static_cast<int>(data.size()));
}

// ── read_request() ────────────────────────────────────────────────────────────

std::string TcpConnection::read_request(int max_bytes) {
    // HTTP/1.1 message framing — what we are reading:
    //
    //   GET /hello HTTP/1.1\r\n         ← request line
    //   Host: localhost:8080\r\n        ← headers
    //   User-Agent: curl/7.88.1\r\n
    //   Accept: */*\r\n
    //   \r\n                            ← blank line: end of headers
    //   [body]                          ← not consumed in Phase 1 (GET has no body)
    //
    // We read bytes in chunks and stop as soon as we see "\r\n\r\n".
    // This prevents us from blocking waiting for body bytes that will never
    // arrive (GET requests have no body per RFC 7230).

    std::string buffer;
    buffer.reserve(1024);

    char chunk[512];
    while (static_cast<int>(buffer.size()) < max_bytes) {
        const int n = read(chunk, static_cast<int>(sizeof(chunk)));

        if (n <= 0) {
            // n == 0: peer sent TCP FIN — connection closed cleanly
            // n < 0:  recv() error (most commonly WSAETIMEDOUT after 5s)
            if (n < 0) {
                const int err = ::WSAGetLastError();
                if (err != WSAETIMEDOUT) {
                    LOG_DEBUG("TcpConnection",
                              "recv() error: " + wsa_error_string(err));
                }
            }
            break;
        }

        buffer.append(chunk, static_cast<size_t>(n));

        // "\r\n\r\n" is the HTTP header terminator defined in RFC 7230 §3.
        // Once we see it, we have all the headers and can stop reading.
        if (buffer.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }

    return buffer;
}

// ── peer_address() ────────────────────────────────────────────────────────────

std::string TcpConnection::peer_address() const {
    char ip[INET_ADDRSTRLEN]{};
    // inet_ntop: converts binary IP address to dotted-decimal string ("192.168.x.x")
    // INET_ADDRSTRLEN (16 bytes) is always large enough for IPv4.
    ::inet_ntop(AF_INET, &peer_addr_.sin_addr, ip, sizeof(ip));
    // ntohs: network byte order → host byte order (for the port number)
    return std::string(ip) + ':' + std::to_string(::ntohs(peer_addr_.sin_port));
}

// ── do_close() ────────────────────────────────────────────────────────────────

void TcpConnection::do_close() noexcept {
    if (fd_ == INVALID_SOCKET) return;

    // Graceful TCP teardown:
    //   shutdown(SD_SEND) sends a FIN to the peer, signalling that we will
    //   send no more data.  The peer's recv() will return 0 (EOF) and it
    //   can finish reading any data still in flight.  We still allow the peer
    //   to send remaining data (TCP half-close).
    //
    //   closesocket() then releases the kernel socket object.
    //
    //   Why not just closesocket() directly?
    //   On Windows, closesocket() with SO_LINGER=0 sends a TCP RST, which
    //   discards any data still buffered on either side.  A clean shutdown()
    //   first ensures the peer receives all buffered data before we close.
    ::shutdown(fd_, SD_SEND);
    ::closesocket(fd_);
    fd_ = INVALID_SOCKET;
}

} // namespace helios::net

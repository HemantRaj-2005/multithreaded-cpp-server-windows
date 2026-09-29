#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/http/http_request.hpp
//  HttpRequest — immutable value object produced by HttpParser.
//
//  Design goals:
//    • Immutable after construction — no setters, all fields const-accessible.
//    • Value semantics — copyable and movable (used in handler dispatch).
//    • No heap allocation except for std::string / std::map members (fine for
//      Phase 2; a zero-copy view-based approach can be added in Phase 6).
//
//  HTTP/1.1 request structure (RFC 7230 §3):
//
//    METHOD SP Request-URI SP HTTP/1.1 CRLF    ← request line
//    Header-Name: Header-Value CRLF            ← zero or more headers
//    CRLF                                      ← end of headers (blank line)
//    [body]                                    ← optional; present only if
//                                                Content-Length > 0
// ─────────────────────────────────────────────────────────────────────────────

#include <map>
#include <string>
#include <string_view>

namespace helios::http {

// Supported HTTP methods.
// Unknown methods are represented as Method::UNKNOWN so the router can
// return 405 Method Not Allowed without crashing.
enum class Method {
    GET,
    POST,
    PUT,
    DELETE_,   // DELETE is a macro on some Windows headers — use DELETE_
    HEAD,
    OPTIONS,
    UNKNOWN
};

// Convert a method string (e.g. "GET") to the Method enum.
// Case-sensitive per RFC 7230 §3.1.1.
Method method_from_string(std::string_view s) noexcept;

// Convert a Method enum back to its string representation.
const char* method_to_string(Method m) noexcept;

// ─────────────────────────────────────────────────────────────────────────────

class HttpRequest {
public:
    // ── Constructors ──────────────────────────────────────────────────────

    // Default-constructed request: UNKNOWN method, empty path, HTTP/1.1.
    HttpRequest() = default;

    // Primary constructor — called by HttpParser after a successful parse.
    HttpRequest(Method              method,
                std::string         path,
                std::string         version,
                std::map<std::string, std::string> headers,
                std::string         body);

    // Value semantics — copy and move are both fine.
    HttpRequest(const HttpRequest&)            = default;
    HttpRequest& operator=(const HttpRequest&) = default;
    HttpRequest(HttpRequest&&)                 = default;
    HttpRequest& operator=(HttpRequest&&)      = default;

    // ── Accessors ─────────────────────────────────────────────────────────

    Method             method()  const noexcept { return method_; }
    const std::string& path()    const noexcept { return path_; }
    const std::string& version() const noexcept { return version_; }
    const std::string& body()    const noexcept { return body_; }

    const std::map<std::string, std::string>& headers() const noexcept {
        return headers_;
    }

    // Look up a header by name.  Header names are case-insensitively compared
    // by HttpParser (stored in lowercase).
    // Returns empty string_view if the header is absent.
    std::string_view header(std::string_view name) const noexcept;

    // Convenience: Content-Length from headers (0 if absent or malformed).
    std::size_t content_length() const noexcept;

    // Is this a valid, fully-parsed request?
    bool is_valid() const noexcept { return method_ != Method::UNKNOWN || !path_.empty(); }

    // Human-readable one-liner: "GET /path HTTP/1.1"
    std::string summary() const;

private:
    Method      method_  = Method::UNKNOWN;
    std::string path_;
    std::string version_ = "HTTP/1.1";
    std::map<std::string, std::string> headers_;
    std::string body_;
};

} // namespace helios::http

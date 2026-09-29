#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/http/http_response.hpp
//  HttpResponse — builder pattern for HTTP/1.1 responses.
//
//  Usage:
//    HttpResponse res = HttpResponse::ok("Hello from Helios!\r\n")
//                           .content_type("text/plain")
//                           .header("X-Server", "Helios");
//    conn.write(res.to_string());
//
//  Design:
//    • Builder returns *this by reference → fluent chaining.
//    • to_string() assembles the wire-format response exactly once.
//    • Content-Length is computed automatically from body().size().
//    • Connection: close is the default in Phase 2 (keep-alive in Phase 5).
//
//  HTTP/1.1 response wire format (RFC 7230 §3):
//
//    HTTP/1.1 STATUS_CODE REASON_PHRASE CRLF    ← status line
//    Header-Name: Header-Value CRLF             ← response headers
//    CRLF                                       ← end of headers
//    [body]                                     ← optional body
// ─────────────────────────────────────────────────────────────────────────────

#include <map>
#include <string>
#include <string_view>

namespace helios::http {

class HttpResponse {
public:
    // ── Constructor ───────────────────────────────────────────────────────
    explicit HttpResponse(int status_code = 200,
                          std::string reason = "OK");

    // ── Named constructors (most common status codes) ─────────────────────

    // 200 OK — optionally with a body string.
    static HttpResponse ok(std::string body = "");

    // 404 Not Found
    static HttpResponse not_found(std::string body = "404 Not Found\r\n");

    // 405 Method Not Allowed
    static HttpResponse method_not_allowed(
        std::string body = "405 Method Not Allowed\r\n");

    // 400 Bad Request
    static HttpResponse bad_request(
        std::string body = "400 Bad Request\r\n");

    // 500 Internal Server Error
    static HttpResponse internal_error(
        std::string body = "500 Internal Server Error\r\n");

    // ── Builder methods (return *this for chaining) ────────────────────────

    // Set or overwrite a response header.
    // Header names are stored as provided (no case normalisation needed here).
    HttpResponse& header(std::string name, std::string value);

    // Set Content-Type header.
    HttpResponse& content_type(std::string_view ct);

    // Set the body and update Content-Length automatically.
    HttpResponse& body(std::string b);

    // ── Accessors ─────────────────────────────────────────────────────────
    int                status_code() const noexcept { return status_code_; }
    const std::string& reason()      const noexcept { return reason_; }
    const std::string& body_str()    const noexcept { return body_; }

    // ── Serialisation ─────────────────────────────────────────────────────

    // Produce the complete HTTP/1.1 wire-format response string.
    // Called exactly once per response, by the connection handler.
    //
    // The generated string always includes:
    //   • Status line
    //   • Content-Length (computed from body_.size())
    //   • Connection: close   (Phase 2; changed to keep-alive in Phase 5)
    //   • Server: Helios/VERSION_STRING
    //   • All headers added via header() / content_type()
    //   • Blank line separator
    //   • Body
    std::string to_string() const;

private:
    int         status_code_;
    std::string reason_;
    std::string body_;
    // Ordered map so headers appear in a deterministic, human-readable order.
    std::map<std::string, std::string> headers_;
};

} // namespace helios::http

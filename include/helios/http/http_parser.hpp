#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/http/http_parser.hpp
//  HttpParser — state-machine HTTP/1.1 request parser.
//
//  Responsibilities:
//    • Parse the raw bytes produced by TcpConnection::read_request() into a
//      fully populated HttpRequest.
//    • Validate the request line (method, path, HTTP version).
//    • Parse headers into a case-normalised (lowercase name) map.
//    • Extract the body if Content-Length > 0 and body bytes are present.
//    • Return a parse result indicating success or the specific failure.
//
//  Design: stateless free function
//    HttpParser is a namespace (no object state).  parse() is a pure function:
//    given a raw string in → HttpRequest + ParseResult out.
//    Stateless parsers are easier to test and require no lifetime management.
//
//  HTTP/1.1 request grammar (simplified, RFC 7230):
//
//    request-line   = method SP request-target SP HTTP-version CRLF
//    header-field   = field-name ":" OWS field-value OWS
//    message-body   = *OCTET
//
//  What we accept (Phase 2 scope):
//    • Methods: GET, POST, PUT, DELETE, HEAD, OPTIONS
//    • Request-target: origin-form only ("/path" or "/path?query")
//    • HTTP-version: HTTP/1.0 or HTTP/1.1
//    • Headers: arbitrary name: value pairs
//    • Body: read up to Content-Length bytes if present in raw input
//
//  What we do NOT parse (deferred):
//    • Transfer-Encoding: chunked (Phase 5+)
//    • Trailers
//    • Multi-line (obs-fold) headers — rejected with 400
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/http/http_request.hpp"
#include <string>
#include <string_view>

namespace helios::http {

// Result of a parse attempt.
enum class ParseResult {
    OK,                  // Request parsed successfully.
    EMPTY_REQUEST,       // Input was empty (client disconnected before sending).
    MISSING_CRLF,        // No \\r\\n\\r\\n header terminator found.
    INVALID_REQUEST_LINE,// Could not tokenise "METHOD PATH VERSION".
    UNSUPPORTED_VERSION, // Not HTTP/1.0 or HTTP/1.1.
    MALFORMED_HEADER,    // A header line has no ':'.
    BODY_TOO_LARGE,      // body exceeds max_body_bytes limit.
};

// Human-readable description of a ParseResult (for logging / 400 messages).
const char* parse_result_to_string(ParseResult r) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
//  HttpParser
//
//  Usage:
//    std::string raw = conn.read_request();
//    HttpRequest req;
//    ParseResult result = HttpParser::parse(raw, req);
//    if (result != ParseResult::OK) { /* send 400 */ }
// ─────────────────────────────────────────────────────────────────────────────

class HttpParser {
public:
    // Not instantiatable — all methods are static.
    HttpParser() = delete;

    // Parse `raw` into `req`.
    // `max_body_bytes` caps the body size to prevent memory exhaustion.
    // Returns ParseResult::OK on success; req is valid only in that case.
    static ParseResult parse(std::string_view raw,
                             HttpRequest&     req,
                             std::size_t      max_body_bytes = 1024 * 1024);

private:
    // ── Internal helpers ──────────────────────────────────────────────────

    // Parse "METHOD PATH HTTP/1.1" from the first line.
    static ParseResult parse_request_line(std::string_view line,
                                          Method&          method,
                                          std::string&     path,
                                          std::string&     version);

    // Parse all header lines into the output map.
    // Header names are lowercased for case-insensitive lookup.
    // Returns false if any header line is malformed.
    static bool parse_headers(std::string_view          headers_block,
                              std::map<std::string, std::string>& out);

    // Trim leading and trailing ASCII whitespace from a string_view.
    static std::string_view trim(std::string_view sv) noexcept;

    // ASCII lowercase (in-place) for a std::string.
    static void to_lower(std::string& s) noexcept;
};

} // namespace helios::http

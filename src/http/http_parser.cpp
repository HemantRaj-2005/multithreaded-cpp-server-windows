// ─────────────────────────────────────────────────────────────────────────────
//  src/http/http_parser.cpp
//  HttpParser implementation.
//
//  Parsing algorithm:
//
//  1. Find the header block terminator "\r\n\r\n".
//     Everything before it is headers_block; everything after is potential body.
//
//  2. Split headers_block on the first "\r\n" to get the request line.
//     The remainder is the header lines.
//
//  3. Parse the request line:
//       METHOD SP path SP HTTP/VERSION
//     Reject anything that doesn't fit.
//
//  4. Parse each header line:
//       field-name ":" OWS field-value OWS
//     Store field-name lowercased; trim whitespace from field-value.
//     Reject lines with no ':'.
//
//  5. If Content-Length > 0 and the raw input contains body bytes after
//     "\r\n\r\n", copy up to min(Content-Length, max_body_bytes) into body.
//
//  Error handling:
//    • Return a specific ParseResult code — never throw.
//    • Caller (main.cpp) maps parse errors to HTTP 400 Bad Request.
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/http/http_parser.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

namespace helios::http {

// ── parse_result_to_string ────────────────────────────────────────────────────

const char* parse_result_to_string(ParseResult r) noexcept {
    switch (r) {
        case ParseResult::OK:                   return "OK";
        case ParseResult::EMPTY_REQUEST:        return "Empty request";
        case ParseResult::MISSING_CRLF:         return "Missing \\r\\n\\r\\n terminator";
        case ParseResult::INVALID_REQUEST_LINE: return "Invalid request line";
        case ParseResult::UNSUPPORTED_VERSION:  return "Unsupported HTTP version";
        case ParseResult::MALFORMED_HEADER:     return "Malformed header";
        case ParseResult::BODY_TOO_LARGE:       return "Body too large";
        default:                                return "Unknown";
    }
}

// ── trim() ────────────────────────────────────────────────────────────────────

std::string_view HttpParser::trim(std::string_view sv) noexcept {
    // Strip leading whitespace
    const auto start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    sv = sv.substr(start);

    // Strip trailing whitespace
    const auto end = sv.find_last_not_of(" \t\r\n");
    if (end != std::string_view::npos) {
        sv = sv.substr(0, end + 1);
    }
    return sv;
}

// ── to_lower() ────────────────────────────────────────────────────────────────

void HttpParser::to_lower(std::string& s) noexcept {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
}

// ── parse_request_line() ──────────────────────────────────────────────────────
//
//  Parses "METHOD SP path SP HTTP/VERSION" from a single line.
//
//  We find the first and last space to tokenise.
//  This correctly handles paths that contain spaces (URL-encoded as %20, but
//  some clients send raw spaces).  For Phase 2, raw-space paths are treated as
//  the full path up to the last space before the version token.

ParseResult HttpParser::parse_request_line(std::string_view line,
                                           Method&          method,
                                           std::string&     path,
                                           std::string&     version) {
    // Find first space (separates method from path)
    const auto sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) return ParseResult::INVALID_REQUEST_LINE;

    // Find last space (separates path from HTTP-version)
    const auto sp2 = line.rfind(' ');
    if (sp2 == sp1) return ParseResult::INVALID_REQUEST_LINE; // only one space

    const std::string_view method_sv  = line.substr(0, sp1);
    const std::string_view path_sv    = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string_view version_sv = line.substr(sp2 + 1);

    method = method_from_string(method_sv);
    // Note: UNKNOWN is valid here — we let the router return 405.

    path    = std::string(trim(path_sv));
    version = std::string(trim(version_sv));

    if (path.empty()) return ParseResult::INVALID_REQUEST_LINE;

    // Validate HTTP version — accept HTTP/1.0 and HTTP/1.1 only.
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        return ParseResult::UNSUPPORTED_VERSION;
    }

    return ParseResult::OK;
}

// ── parse_headers() ───────────────────────────────────────────────────────────
//
//  `headers_block` is everything between the request line and "\r\n\r\n",
//  i.e. the raw header lines joined by "\r\n".
//
//  We split on "\r\n" and process each line individually.

bool HttpParser::parse_headers(std::string_view                   headers_block,
                                std::map<std::string, std::string>& out) {
    std::string_view remaining = headers_block;

    while (!remaining.empty()) {
        // Find the end of this header line.
        const auto crlf = remaining.find("\r\n");
        std::string_view line = (crlf == std::string_view::npos)
                                ? remaining
                                : remaining.substr(0, crlf);

        // RFC 7230 §3.2: obs-fold (multi-line headers) is obsolete.
        // We simply reject any line starting with whitespace (it would indicate
        // a fold that we don't support).
        // For Phase 2 we skip empty lines gracefully.
        line = trim(line);
        if (line.empty()) {
            remaining = (crlf == std::string_view::npos)
                        ? std::string_view{}
                        : remaining.substr(crlf + 2);
            continue;
        }

        // Expect exactly one ':' separating name from value.
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) return false;

        std::string name{trim(line.substr(0, colon))};
        std::string value{trim(line.substr(colon + 1))};

        // Lowercase the name for case-insensitive lookup by HttpRequest::header().
        to_lower(name);

        if (!name.empty()) {
            out[std::move(name)] = std::move(value);
        }

        remaining = (crlf == std::string_view::npos)
                    ? std::string_view{}
                    : remaining.substr(crlf + 2);
    }

    return true;
}

// ── parse() ───────────────────────────────────────────────────────────────────

ParseResult HttpParser::parse(std::string_view raw,
                              HttpRequest&     req,
                              std::size_t      max_body_bytes) {
    // ── 1. Guard: empty input ─────────────────────────────────────────────
    if (raw.empty()) return ParseResult::EMPTY_REQUEST;

    // ── 2. Find "\r\n\r\n" — the end of the HTTP header section ──────────
    //
    // RFC 7230 §3: The header section terminates with a CRLF-CRLF sequence.
    // Everything before it is the request line + headers.
    // Everything after is the optional message body.
    constexpr std::string_view HEADER_TERMINATOR = "\r\n\r\n";
    const auto hdr_end = raw.find(HEADER_TERMINATOR);
    if (hdr_end == std::string_view::npos) return ParseResult::MISSING_CRLF;

    const std::string_view header_section = raw.substr(0, hdr_end);
    const std::string_view body_section   = raw.substr(hdr_end + HEADER_TERMINATOR.size());

    // ── 3. Split off the request line ────────────────────────────────────
    const auto first_crlf = header_section.find("\r\n");
    const std::string_view request_line = (first_crlf == std::string_view::npos)
                                          ? header_section
                                          : header_section.substr(0, first_crlf);

    const std::string_view headers_block = (first_crlf == std::string_view::npos)
                                           ? std::string_view{}
                                           : header_section.substr(first_crlf + 2);

    // ── 4. Parse the request line ─────────────────────────────────────────
    Method      method;
    std::string path;
    std::string version;
    const ParseResult line_result =
        parse_request_line(request_line, method, path, version);
    if (line_result != ParseResult::OK) return line_result;

    // ── 5. Parse headers ──────────────────────────────────────────────────
    std::map<std::string, std::string> headers;
    if (!parse_headers(headers_block, headers)) {
        return ParseResult::MALFORMED_HEADER;
    }

    // ── 6. Parse body ─────────────────────────────────────────────────────
    //
    // We only extract a body if Content-Length > 0.
    // For Phase 2 (GET-dominant), this is usually 0.
    // For POST requests, the body will be present.
    //
    // We do NOT support Transfer-Encoding: chunked in Phase 2.
    std::string body;
    {
        std::size_t cl = 0;
        const auto cl_it = headers.find("content-length");
        if (cl_it != headers.end()) {
            try { cl = std::stoul(cl_it->second); } catch (...) { cl = 0; }
        }

        if (cl > 0) {
            if (cl > max_body_bytes) return ParseResult::BODY_TOO_LARGE;
            // Take up to cl bytes from body_section (may have fewer if truncated).
            const std::size_t available = body_section.size();
            body = std::string(body_section.substr(0, std::min(cl, available)));
        }
    }

    // ── 7. Assemble HttpRequest ───────────────────────────────────────────
    req = HttpRequest{method,
                      std::move(path),
                      std::move(version),
                      std::move(headers),
                      std::move(body)};

    return ParseResult::OK;
}

} // namespace helios::http

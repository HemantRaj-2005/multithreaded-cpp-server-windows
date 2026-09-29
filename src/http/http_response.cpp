// ─────────────────────────────────────────────────────────────────────────────
//  src/http/http_response.cpp
//  HttpResponse implementation.
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/http/http_response.hpp"
#include "helios/version.hpp"

#include <sstream>

namespace helios::http {

// ── Constructor ───────────────────────────────────────────────────────────────

HttpResponse::HttpResponse(int status_code, std::string reason)
    : status_code_{status_code}
    , reason_     {std::move(reason)}
{}

// ── Named constructors ────────────────────────────────────────────────────────

HttpResponse HttpResponse::ok(std::string b) {
    HttpResponse r{200, "OK"};
    r.body(std::move(b));
    return r;
}

HttpResponse HttpResponse::not_found(std::string b) {
    HttpResponse r{404, "Not Found"};
    r.body(std::move(b));
    return r;
}

HttpResponse HttpResponse::method_not_allowed(std::string b) {
    HttpResponse r{405, "Method Not Allowed"};
    r.body(std::move(b));
    return r;
}

HttpResponse HttpResponse::bad_request(std::string b) {
    HttpResponse r{400, "Bad Request"};
    r.body(std::move(b));
    return r;
}

HttpResponse HttpResponse::internal_error(std::string b) {
    HttpResponse r{500, "Internal Server Error"};
    r.body(std::move(b));
    return r;
}

// ── Builder methods ───────────────────────────────────────────────────────────

HttpResponse& HttpResponse::header(std::string name, std::string value) {
    headers_[std::move(name)] = std::move(value);
    return *this;
}

HttpResponse& HttpResponse::content_type(std::string_view ct) {
    headers_["Content-Type"] = std::string{ct};
    return *this;
}

HttpResponse& HttpResponse::body(std::string b) {
    body_ = std::move(b);
    return *this;
}

// ── to_string() ───────────────────────────────────────────────────────────────
//
//  Assembles the complete HTTP/1.1 response wire string.
//
//  Header ordering:
//    1. Status line
//    2. Content-Type   (if set)
//    3. Content-Length (always; computed from body_.size())
//    4. Connection: close   (Phase 2 default; Phase 5 switches to keep-alive)
//    5. Server: Helios/VERSION_STRING
//    6. Any additional headers added via header()
//    7. CRLF separator
//    8. Body
//
//  Why Content-Length is mandatory:
//    HTTP/1.1 clients need Content-Length (or chunked Transfer-Encoding) to
//    know where the message body ends.  Without it, the client reads until the
//    connection closes (only valid with Connection: close, which is fragile).
//    We always compute and include it.

std::string HttpResponse::to_string() const {
    std::ostringstream oss;

    // Status line
    oss << "HTTP/1.1 " << status_code_ << ' ' << reason_ << "\r\n";

    // Content-Type (from headers_ if set, else default to text/plain)
    auto ct_it = headers_.find("Content-Type");
    if (ct_it != headers_.end()) {
        oss << "Content-Type: " << ct_it->second << "\r\n";
    } else {
        oss << "Content-Type: text/plain\r\n";
    }

    // Content-Length — always present
    oss << "Content-Length: " << body_.size() << "\r\n";

    // Connection — Phase 2 always closes (keep-alive in Phase 5)
    oss << "Connection: close\r\n";

    // Server identification
    oss << "Server: Helios/" << helios::VERSION_STRING << "\r\n";

    // Additional headers (skip Content-Type; already emitted above)
    for (const auto& [name, value] : headers_) {
        if (name == "Content-Type") continue;
        oss << name << ": " << value << "\r\n";
    }

    // Blank line: end of headers
    oss << "\r\n";

    // Body
    oss << body_;

    return oss.str();
}

} // namespace helios::http

// ─────────────────────────────────────────────────────────────────────────────
//  src/http/http_request.cpp
//  HttpRequest implementation.
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/http/http_request.hpp"

#include <algorithm>   // std::transform
#include <cctype>      // std::tolower
#include <stdexcept>

namespace helios::http {

// ── method_from_string ────────────────────────────────────────────────────────

Method method_from_string(std::string_view s) noexcept {
    if (s == "GET")     return Method::GET;
    if (s == "POST")    return Method::POST;
    if (s == "PUT")     return Method::PUT;
    if (s == "DELETE")  return Method::DELETE_;
    if (s == "HEAD")    return Method::HEAD;
    if (s == "OPTIONS") return Method::OPTIONS;
    return Method::UNKNOWN;
}

// ── method_to_string ──────────────────────────────────────────────────────────

const char* method_to_string(Method m) noexcept {
    switch (m) {
        case Method::GET:     return "GET";
        case Method::POST:    return "POST";
        case Method::PUT:     return "PUT";
        case Method::DELETE_: return "DELETE";
        case Method::HEAD:    return "HEAD";
        case Method::OPTIONS: return "OPTIONS";
        default:              return "UNKNOWN";
    }
}

// ── HttpRequest constructor ───────────────────────────────────────────────────

HttpRequest::HttpRequest(Method              method,
                         std::string         path,
                         std::string         version,
                         std::map<std::string, std::string> headers,
                         std::string         body)
    : method_ {method}
    , path_   {std::move(path)}
    , version_{std::move(version)}
    , headers_{std::move(headers)}
    , body_   {std::move(body)}
{}

// ── header() ─────────────────────────────────────────────────────────────────

std::string_view HttpRequest::header(std::string_view name) const noexcept {
    // Header names are stored lowercased by HttpParser.
    // We lowercase the lookup key here so callers can pass any casing.
    std::string key{name};
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto it = headers_.find(key);
    if (it == headers_.end()) return {};
    return it->second;
}

// ── content_length() ─────────────────────────────────────────────────────────

std::size_t HttpRequest::content_length() const noexcept {
    const auto cl = header("content-length");
    if (cl.empty()) return 0;
    try {
        // stoul throws on empty or non-numeric — catch and return 0.
        return std::stoul(std::string{cl});
    } catch (...) {
        return 0;
    }
}

// ── summary() ────────────────────────────────────────────────────────────────

std::string HttpRequest::summary() const {
    return std::string(method_to_string(method_)) + ' ' + path_ + ' ' + version_;
}

} // namespace helios::http

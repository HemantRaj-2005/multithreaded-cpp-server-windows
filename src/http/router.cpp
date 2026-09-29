// ─────────────────────────────────────────────────────────────────────────────
//  src/http/router.cpp
//  Router implementation.
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/http/router.hpp"

namespace helios::http {

// ── add_route() ───────────────────────────────────────────────────────────────

void Router::add_route(Method method, std::string path, Handler handler) {
    routes_[std::move(path)][method] = std::move(handler);
}

// ── dispatch() ────────────────────────────────────────────────────────────────
//
//  Lookup sequence:
//    1. Find the path in routes_.
//       → Not found? Return 404.
//    2. Find the method inside that path's method map.
//       → Not found? The path exists but not for this method → 405.
//    3. Found? Invoke the handler and return its response.
//
//  Why 404 vs 405?
//    RFC 7231 §6.5.4 (404): The target resource was not found.
//    RFC 7231 §6.5.5 (405): The request method is known but not supported for
//    the target resource.  405 MUST include an Allow header listing the
//    supported methods for that resource.
//
//    Returning the correct code matters for correctness and for API clients
//    that distinguish "wrong URL" from "wrong HTTP verb".

HttpResponse Router::dispatch(const HttpRequest& req) const {
    const auto path_it = routes_.find(req.path());

    // ── 404 Not Found ─────────────────────────────────────────────────────
    if (path_it == routes_.end()) {
        return HttpResponse::not_found(
            "404 Not Found: " + req.path() + "\r\n");
    }

    const MethodMap& method_map = path_it->second;
    const auto       method_it  = method_map.find(req.method());

    // ── 405 Method Not Allowed ────────────────────────────────────────────
    if (method_it == method_map.end()) {
        // Build the Allow header value: comma-separated list of registered methods.
        std::string allow;
        for (const auto& [m, _h] : method_map) {
            if (!allow.empty()) allow += ", ";
            allow += method_to_string(m);
        }
        return HttpResponse::method_not_allowed(
                   "405 Method Not Allowed\r\n")
               .header("Allow", allow);
    }

    // ── Invoke handler ────────────────────────────────────────────────────
    return method_it->second(req);
}

// ── route_count() ─────────────────────────────────────────────────────────────

std::size_t Router::route_count() const noexcept {
    std::size_t count = 0;
    for (const auto& [_path, methods] : routes_) {
        count += methods.size();
    }
    return count;
}

} // namespace helios::http

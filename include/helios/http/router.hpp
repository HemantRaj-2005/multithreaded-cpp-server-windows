#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/http/router.hpp
//  Router — maps (Method, path) pairs to handler functions.
//
//  Design:
//    • Routes are registered with add_route() before the server starts.
//    • dispatch() is called per request; it matches method + exact path and
//      invokes the corresponding handler, or returns a 404/405.
//    • Handler type: std::function<HttpResponse(const HttpRequest&)>
//    • Storage: std::map<std::string, HandlerMap> keyed on path.
//      HandlerMap maps Method → Handler.
//      This gives O(log n) path lookup and O(1) method lookup.
//
//  Why linear-scan / std::map rather than a radix trie?
//    Phase 2 has only 3 static routes.  A radix trie adds implementation
//    complexity with no measurable benefit at this scale.  The Phase 4 upgrade
//    (path parameters like /users/{id}) will introduce a trie at that point.
//
//  Thread-safety:
//    Routes are registered once during startup (before threads start in Phase 3).
//    After that, dispatch() is read-only.  No locking needed in Phase 2-3.
//    Phase 4 adds a std::shared_mutex if runtime route registration is needed.
//
//  Usage:
//    Router router;
//    router.add_route(Method::GET, "/", handler_root);
//    router.add_route(Method::GET, "/health", handler_health);
//
//    HttpResponse res = router.dispatch(request);
//    conn.write(res.to_string());
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/http/http_request.hpp"
#include "helios/http/http_response.hpp"

#include <functional>
#include <map>
#include <string>

namespace helios::http {

// Type of a request handler function.
// Takes the fully-parsed request; returns a complete response.
// Must be pure (no side effects beyond building the response) for Phase 2.
// Side effects (DB calls, cache access) are introduced in Phase 9-10.
using Handler = std::function<HttpResponse(const HttpRequest&)>;

class Router {
public:
    Router() = default;

    // Non-copyable (handlers may capture state).
    Router(const Router&)            = delete;
    Router& operator=(const Router&) = delete;
    Router(Router&&)                 = default;
    Router& operator=(Router&&)      = default;

    // ── Route registration ────────────────────────────────────────────────

    // Register a handler for the given method + exact path.
    // If a handler already exists for this (method, path) pair, it is replaced.
    // IMPORTANT: call add_route() only before starting the accept loop.
    void add_route(Method method, std::string path, Handler handler);

    // Shorthand helpers for the common HTTP methods.
    void get   (std::string path, Handler h) { add_route(Method::GET,    std::move(path), std::move(h)); }
    void post  (std::string path, Handler h) { add_route(Method::POST,   std::move(path), std::move(h)); }
    void put   (std::string path, Handler h) { add_route(Method::PUT,    std::move(path), std::move(h)); }
    void del   (std::string path, Handler h) { add_route(Method::DELETE_, std::move(path), std::move(h)); }

    // ── Dispatch ──────────────────────────────────────────────────────────

    // Match req.path() against registered routes.
    // Returns:
    //   • The handler's response if a match is found.
    //   • 405 Method Not Allowed if the path exists but for a different method.
    //   • 404 Not Found if no route matches the path at all.
    HttpResponse dispatch(const HttpRequest& req) const;

    // Number of registered routes (for logging / tests).
    std::size_t route_count() const noexcept;

private:
    // routes_[path][method] = handler
    using MethodMap = std::map<Method, Handler>;
    std::map<std::string, MethodMap> routes_;
};

} // namespace helios::http

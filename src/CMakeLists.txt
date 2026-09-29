# ─────────────────────────────────────────────────────────────────────────────
#  src/CMakeLists.txt
#
#  Phase 0: helios_core was an INTERFACE library (header-only components).
#  Phase 1: helios_core becomes a STATIC library (TcpServer, TcpConnection).
#  Phase 2: adds HTTP abstraction layer (HttpParser, HttpRequest, HttpResponse,
#            Router).
#
#  To add a new source file in future phases:
#    Just append it to the helios_core source list below.
#    No other CMake changes needed.
# ─────────────────────────────────────────────────────────────────────────────

# ── helios_core — static library ─────────────────────────────────────────────
# All non-main server code lives here.
# Both helios (executable) and helios_tests link against helios_core.
# This means tests can exercise any class without pulling in main().
add_library(helios_core STATIC
    # Phase 1 — TCP networking
    net/tcp_connection.cpp
    net/tcp_server.cpp

    # Phase 2 — HTTP abstraction layer
    http/http_request.cpp
    http/http_response.cpp
    http/http_parser.cpp
    http/router.cpp

    # Phase 3 — Thread pool
    concurrency/thread_pool.cpp
)

target_include_directories(helios_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(helios_core
    PUBLIC
        ${HELIOS_PLATFORM_LIBS}   # ws2_32 on Windows
)

# ── helios — server executable ────────────────────────────────────────────────
add_executable(helios main.cpp)

target_link_libraries(helios
    PRIVATE
        helios_core
)

# ─────────────────────────────────────────────────────────────────────────────
#  src/CMakeLists.txt
#
#  Phase 0: helios_core was an INTERFACE library (header-only components).
#  Phase 1: helios_core becomes a STATIC library now that we have .cpp files.
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

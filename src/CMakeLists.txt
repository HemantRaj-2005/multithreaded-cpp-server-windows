# ─────────────────────────────────────────────────────────────────────────────
#  src/CMakeLists.txt
#  Defines two targets:
#    helios_core — interface/static library (linked by helios and helios_tests)
#    helios      — the server executable
#
#  Phase 0: helios_core is an INTERFACE library because there are no .cpp files
#  yet (Logger and Config are header-only). From Phase 1 onward it becomes a
#  proper STATIC library as we add tcp_server.cpp, parser.cpp, etc.
#  To upgrade: change INTERFACE → STATIC and add source files to the list.
# ─────────────────────────────────────────────────────────────────────────────

# ── helios_core — INTERFACE library (Phase 0) ─────────────────────────────────
# An INTERFACE target has no compiled object files but propagates its
# INTERFACE_INCLUDE_DIRECTORIES and INTERFACE_LINK_LIBRARIES to every consumer.
add_library(helios_core INTERFACE)

target_include_directories(helios_core
    INTERFACE
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(helios_core
    INTERFACE
        ${HELIOS_PLATFORM_LIBS}   # ws2_32 on Windows; empty elsewhere
)

# ── helios — server executable ────────────────────────────────────────────────
add_executable(helios main.cpp)

target_link_libraries(helios
    PRIVATE
        helios_core
)

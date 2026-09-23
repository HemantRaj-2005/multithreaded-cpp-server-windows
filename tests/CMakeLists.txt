# ─────────────────────────────────────────────────────────────────────────────
#  tests/CMakeLists.txt
#  Phase 0: Placeholder test target — no tests yet.
#  Phase 1+ will add unit tests for the HTTP parser, router, etc.
#
#  We use a custom lightweight test runner rather than pulling in a test
#  framework dependency in Phase 0.  If GoogleTest or Catch2 is introduced
#  later we will document the trade-off at that point.
# ─────────────────────────────────────────────────────────────────────────────

add_executable(helios_tests
    test_main.cpp
)

target_link_libraries(helios_tests
    PRIVATE
        helios_core
)

target_include_directories(helios_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

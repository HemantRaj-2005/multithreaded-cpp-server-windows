cmake_minimum_required(VERSION 3.20)

# ─────────────────────────────────────────────────────────────────────────────
#  Helios HTTP Server
#  Phase 3 — Multithreaded Thread Pool
#
#  Build commands (from repo root):
#    cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
#    cmake --build build --config Debug
#    .\build\Debug\helios.exe          (MSVC)
#    .\build\helios.exe                (MinGW)
# ─────────────────────────────────────────────────────────────────────────────
project(helios
    VERSION      0.1.0
    DESCRIPTION  "Helios — Production-grade multithreaded HTTP server in C++17"
    LANGUAGES    CXX
)

# ── C++ Standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)   # -std=c++17 not -std=gnu++17

# ── Export compile commands (useful for clangd / IDE integration) ─────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Compiler warning flags ────────────────────────────────────────────────────
if(MSVC)
    # /W4    — level-4 warnings (comprehensive)
    # /WX    — treat warnings as errors
    # /permissive- — enforce strict ISO C++ conformance
    add_compile_options(/W4 /WX /permissive-)

    # Suppress MSVC's noise about standard functions it deems "unsafe"
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # -Wall -Wextra -Werror -pedantic — comprehensive warnings as errors
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

# ── Platform libraries ────────────────────────────────────────────────────────
# Winsock (Phase 1+): ws2_32
# Threads (Phase 3+): required for std::thread on MinGW (links libpthread).
#   On MSVC, Threads::Threads is a no-op (std::thread uses Win32 threads natively).
find_package(Threads REQUIRED)
set(HELIOS_PLATFORM_LIBS "")
if(WIN32)
    list(APPEND HELIOS_PLATFORM_LIBS ws2_32)
endif()
list(APPEND HELIOS_PLATFORM_LIBS Threads::Threads)

# ── Global include path ───────────────────────────────────────────────────────
# All targets get ${CMAKE_SOURCE_DIR}/include on their include path.
# Individual targets also set it via target_include_directories for clarity.
include_directories(${CMAKE_SOURCE_DIR}/include)

# ── Sub-projects ──────────────────────────────────────────────────────────────
add_subdirectory(src)
add_subdirectory(tests)

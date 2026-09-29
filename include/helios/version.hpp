#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/version.hpp
//  Single source of truth for the Helios version.
//
//  Usage:
//    #include "helios/version.hpp"
//    std::cout << helios::VERSION_STRING;   // "0.1.0"
// ─────────────────────────────────────────────────────────────────────────────

namespace helios {

constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;

// Human-readable string version — kept in sync with the integers above.
constexpr const char* VERSION_STRING = "0.1.0";

// Phase identifier — updated each time a new phase is completed.
constexpr int    PHASE         = 3;
constexpr const char* PHASE_NAME = "Multithreaded Thread Pool";

} // namespace helios

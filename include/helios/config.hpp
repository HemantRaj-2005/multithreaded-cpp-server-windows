#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/config.hpp
//  INI-style configuration loader for Helios.
//
//  File format:
//    # Comment
//    [section]
//    key = value    # inline comment
//
//  Usage:
//    auto& cfg = helios::Config::instance();
//    cfg.load("config/server.conf");          // returns false if not found
//
//    int  port    = cfg.get_int   ("server",  "port",    8080);
//    bool ka      = cfg.get_bool  ("server",  "keep_alive", true);
//    auto name    = cfg.get_string("server",  "name",    "helios");
//
//  Design decisions:
//    • Header-only for Phase 0 simplicity.  The parser implementation lives
//      inside the header behind `inline`; for a large project we would split
//      into a .cpp.  We do so explicitly in src/config.cpp for linkage
//      correctness but the logic stays readable here.
//    • Singleton — config is loaded once at startup and then read-only.
//      Read-only access after load() means we only need the mutex during load
//      and get_*; future phases can relax this to a shared_mutex if profiling
//      warrants it.
//    • Keys are stored as "section.key" strings in a std::map for O(log n)
//      lookup.  With a realistic config file of ~50 entries this is negligible.
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace helios {

class Config {
public:
    // Meyer's singleton.
    static Config& instance() noexcept {
        static Config inst;
        return inst;
    }

    Config(const Config&)            = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&)                 = delete;
    Config& operator=(Config&&)      = delete;

    // ── Load ──────────────────────────────────────────────────────────────
    // Parse the INI file at `path`.
    // Returns true on success, false if the file cannot be opened.
    // Calling load() a second time replaces all previously loaded values.
    bool load(std::string_view path) {
        std::ifstream file{std::string(path)};
        if (!file.is_open()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();

        std::string section = "global";
        std::string line;

        while (std::getline(file, line)) {
            line = trim(line);

            // Skip blank lines and comment lines
            if (line.empty() || line.front() == '#' || line.front() == ';') {
                continue;
            }

            // Section header: [server]
            if (line.front() == '[' && line.back() == ']') {
                section = trim(line.substr(1, line.size() - 2));
                continue;
            }

            // Key = Value pair
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;  // malformed line — skip

            std::string key   = trim(line.substr(0, eq));
            std::string value = trim(line.substr(eq + 1));

            // Strip inline comment from value
            const auto hash = value.find('#');
            if (hash != std::string::npos) {
                value = trim(value.substr(0, hash));
            }

            entries_[compound_key(section, key)] = std::move(value);
        }

        loaded_ = true;
        return true;
    }

    bool is_loaded() const noexcept { return loaded_; }

    // ── Accessors ─────────────────────────────────────────────────────────

    // Returns the raw string value for section.key, or default_value.
    std::string get_string(std::string_view section,
                           std::string_view key,
                           std::string      default_value = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entries_.find(compound_key(section, key));
        return (it != entries_.end()) ? it->second : std::move(default_value);
    }

    // Returns the value parsed as int, or default_value on missing/parse error.
    int get_int(std::string_view section,
                std::string_view key,
                int              default_value = 0) const {
        const auto s = get_string(section, key, "");
        if (s.empty()) return default_value;
        try {
            return std::stoi(s);
        } catch (...) {
            return default_value;
        }
    }

    // Returns the value parsed as bool.
    // "true" / "1" / "yes" / "on"  → true  (case-insensitive)
    // anything else                 → false
    bool get_bool(std::string_view section,
                  std::string_view key,
                  bool             default_value = false) const {
        auto s = get_string(section, key, "");
        if (s.empty()) return default_value;
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return (s == "true" || s == "1" || s == "yes" || s == "on");
    }

private:
    Config()  = default;
    ~Config() = default;

    // ── Helpers ───────────────────────────────────────────────────────────

    static std::string compound_key(std::string_view section, std::string_view key) {
        std::string k;
        k.reserve(section.size() + 1 + key.size());
        k.append(section);
        k += '.';
        k.append(key);
        return k;
    }

    static std::string trim(std::string s) {
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    }

    mutable std::mutex              mutex_;
    std::map<std::string, std::string> entries_;
    bool                            loaded_{false};
};

} // namespace helios

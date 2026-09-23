# Helios HTTP Server — Low-Level Design (LLD)

> **Status:** Living document — updated at the end of each phase.  
> **Current phase:** 0 — Project Foundation

This document dives below the HLD into specific class designs, data structures, algorithms, and API contracts for each component.

---

## Phase 0 — Logger

### `helios::LogLevel` (enum class)

```cpp
enum class LogLevel : int {
    TRACE = 0, DEBUG = 1, INFO = 2,
    WARN  = 3, ERROR = 4, FATAL = 5, OFF = 6
};
```

**Design note:** Backed by `int` so that numeric comparison (`lvl < min_level_`) works without casts.

---

### `helios::Logger` (singleton)

```
┌──────────────────────────────────────┐
│  Logger                              │
├──────────────────────────────────────┤
│ - mutex_    : std::mutex             │
│ - min_level_: LogLevel = INFO        │
├──────────────────────────────────────┤
│ + instance() : Logger&               │
│ + set_level(LogLevel) : void         │
│ + level() : LogLevel                 │
│ + log(LogLevel, component, msg): void│
└──────────────────────────────────────┘
```

**Thread-safety contract:**
- `log()` acquires `mutex_` before writing to `cerr`.
- `set_level()` acquires `mutex_` before modifying `min_level_`.
- `level()` acquires `mutex_` for read consistency (can be relaxed to `std::atomic` in Phase 6 if profiled as hot).

**Log format:**
```
[YYYY-MM-DD HH:MM:SS] [LEVEL] [component] message\n
```

**Timestamp resolution:** seconds. Sub-second resolution (milliseconds) will be added in Phase 6 when request latency logging requires it.

---

### `helios::Config` (singleton)

```
┌──────────────────────────────────────────┐
│  Config                                  │
├──────────────────────────────────────────┤
│ - mutex_  : std::mutex                   │
│ - entries_: std::map<string, string>     │
│ - loaded_ : bool = false                 │
├──────────────────────────────────────────┤
│ + instance()  : Config&                  │
│ + load(path)  : bool                     │
│ + get_string(section, key, default)      │
│ + get_int   (section, key, default)      │
│ + get_bool  (section, key, default)      │
│ + is_loaded() : bool                     │
└──────────────────────────────────────────┘
```

**Key storage:** `"section.key"` — e.g., `"server.port"`, `"logging.level"`.  
Using `std::map` gives O(log n) lookup. With ≤ 100 config keys this is negligible. A `std::unordered_map` would give O(1) average but is overkill here.

**INI parse algorithm:**
```
state: current_section = "global"
for each line:
    trim whitespace
    if empty or starts with # or ; → skip
    if starts with [ and ends with ] → update current_section
    if contains = → split on first =; strip inline # comment; store
```

---

## Phase 1 — TcpServer (to be filled in)

*This section will be written during Phase 1 implementation.*

Components to design:
- `TcpServer` — socket lifecycle, bind, listen, accept loop
- `TcpConnection` — RAII socket wrapper, read/write helpers
- Error handling strategy (Winsock error codes → human-readable strings)

---

## Phase 2 — HTTP Layer (to be filled in)

*This section will be written during Phase 2 implementation.*

Components to design:
- `HttpParser` — state machine for parsing HTTP/1.1 request lines, headers, body
- `HttpRequest` — immutable value object
- `HttpResponse` — builder pattern
- `Router` — radix trie vs. `std::map` vs. linear scan; route parameter extraction

---

## Phase 3 — ThreadPool (to be filled in)

*This section will be written during Phase 3 implementation.*

Topics:
- `WorkQueue<T>` — generic queue with mutex + condition variable
- `ThreadPool` — fixed thread count, graceful shutdown protocol
- Why we chose N threads = `std::thread::hardware_concurrency()`
- Shutdown sequence: poison pill vs. atomic stop flag

---

## Phase 4–14 (to be filled in)

Each subsequent phase will add an LLD section here covering:
- Class interfaces
- Data structures chosen (and why)
- Algorithms
- Concurrency contracts
- Error handling

---

*Last updated: Phase 0 — 2026-09-24*

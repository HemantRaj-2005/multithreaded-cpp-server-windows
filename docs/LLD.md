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

## Phase 1 — TCP Networking Layer

### Socket lifecycle

```
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
     │
     ▼
setsockopt(SO_REUSEADDR)   ← allow rapid restart without EADDRINUSE
     │
     ▼
bind({INADDR_ANY, port})   ← attach to local IP:port
     │
     ▼
listen(backlog=128)         ← OS begins queuing incoming connections
     │
     ▼  ┌─────────────────────────────────────────────────────┐
     │  │ Accept loop                                         │
     │  │   select(server_fd, timeout=500ms)                  │
     │  │     → timeout? check running_, continue             │
     │  │     → readable? proceed to accept()                 │
     │  │   accept(server_fd, &client_addr)                   │
     │  │     → returns new client_fd + peer address          │
     │  │   TcpConnection conn{client_fd, client_addr}        │
     │  │   handler(move(conn))                               │
     │  └─────────────────────────────────────────────────────┘
     │
     ▼
closesocket(server_fd)
```

---

### `helios::net::WinsockGuard`

```
┌───────────────────────────────────────────────┐
│  WinsockGuard                                 │
├───────────────────────────────────────────────┤
│ - initialized_ : bool                         │
│ - error_       : string                       │
├───────────────────────────────────────────────┤
│ + WinsockGuard()   → WSAStartup(MAKEWORD(2,2))│
│ + ~WinsockGuard()  → WSACleanup()             │
│ + ok()       : bool                           │
│ + error_message() : string                    │
├───────────────────────────────────────────────┤
│ Non-copyable, non-movable                     │
│ Exactly ONE per process                       │
└───────────────────────────────────────────────┘
```

**Invariants:**
- If `ok() == true`, all Winsock calls are valid until the guard is destroyed.
- Destructor is a no-op if `ok() == false` (no double-cleanup).

---

### `helios::net::wsa_error_string(int code = -1)`

Free function. Calls `FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM)` to convert a Winsock error code into a human-readable string.  
Default argument `-1` → calls `WSAGetLastError()` internally.

Output format: `"Connection refused (WSA error 10061)"`

---

### `helios::net::TcpConnection`

```
┌──────────────────────────────────────────────────┐
│  TcpConnection                                   │
├──────────────────────────────────────────────────┤
│ - fd_        : SOCKET      (INVALID_SOCKET when  │
│ - peer_addr_ : sockaddr_in  moved-from)          │
├──────────────────────────────────────────────────┤
│ + TcpConnection(fd, peer_addr)                   │
│     → setsockopt(SO_RCVTIMEO, 5000ms)            │
│ + ~TcpConnection()                               │
│     → shutdown(SD_SEND) + closesocket()          │
│                                                  │
│ + read(buf, len)  : int      ← recv()            │
│ + write(data, len): bool     ← send() loop       │
│ + write(string_view): bool                       │
│ + read_request(max=8192): string                 │
│     → reads until \r\n\r\n or max_bytes          │
│ + peer_address() : string    ← "IP:port"         │
│ + is_valid()     : bool                          │
│ + fd()           : SOCKET                        │
├──────────────────────────────────────────────────┤
│ Non-copyable / Movable                           │
│ Move sets fd_ = INVALID_SOCKET (no double-close) │
└──────────────────────────────────────────────────┘
```

**Partial send contract:**
`write()` loops until all `len` bytes are sent. A single `send()` call may transmit fewer bytes than requested when the kernel send buffer is full.

**Receive timeout:**
`SO_RCVTIMEO = 5000ms` is set in the constructor. Prevents a slow client from monopolising the server's thread.

---

### `helios::net::TcpServer`

```
┌──────────────────────────────────────────────────┐
│  TcpServer                                       │
├──────────────────────────────────────────────────┤
│ - server_fd_ : SOCKET                            │
│ - port_      : int                               │
│ - backlog_   : int = 128                         │
│ - running_   : atomic<bool>                      │
├──────────────────────────────────────────────────┤
│ + TcpServer(port, backlog=128)                   │
│ + ~TcpServer() → closesocket(server_fd_)         │
│                                                  │
│ + start() : bool                                 │
│     → socket() → setsockopt() → bind() → listen()│
│ + run(ConnectionHandler handler) : void          │
│     → select loop → accept → handler(move(conn)) │
│ + stop() : void        ← atomic, signal-safe     │
│ + is_running() : bool                            │
│ + port() : int                                   │
├──────────────────────────────────────────────────┤
│ ConnectionHandler = std::function<void(TcpConnection)> │
│ Non-copyable, non-movable                        │
└──────────────────────────────────────────────────┘
```

**`running_` atomicity:**
`stop()` writes `running_ = false` via `std::atomic::store()`.  
`run()` reads `running_` via `std::atomic::load()`.  
`std::atomic` provides the memory ordering guarantee needed for cross-thread/signal-handler visibility.

**Phase 3 upgrade path (documented now, implemented later):**
```cpp
// Phase 1 (current):
handler(std::move(conn));   // synchronous — blocks the accept loop

// Phase 3 (future):
thread_pool_.submit(std::move(conn));  // async — accept loop continues immediately
```
The TcpServer public API does not change between Phase 1 and Phase 3.

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

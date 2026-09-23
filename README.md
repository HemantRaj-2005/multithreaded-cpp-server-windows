# Helios HTTP Server

> A production-oriented, multithreaded HTTP/1.1 server built from scratch in C++17 on Windows.  
> Built phase-by-phase to teach systems programming, networking, concurrency, and production engineering.

---

## Current Phase

**Phase 0 — Project Foundation** ✅  
`v0.1.0`

---

## Quick Start

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | ≥ 3.20 | [cmake.org](https://cmake.org) |
| MSVC | VS 2019+ | C++17 support required |
| MinGW | GCC 10+ | Alternative compiler |
| Git | Any | |

### Build (MSVC)

```powershell
# Clone
git clone <repo-url> helios
cd helios

# Configure
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug

# Run
.\build\Debug\helios.exe
```

### Build (MinGW)

```bash
cmake -B build-mingw -S . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-mingw
.\build-mingw\helios.exe
```

### Run Tests

```powershell
.\build\Debug\helios_tests.exe
```

### Custom Config Path

```powershell
.\build\Debug\helios.exe path\to\my.conf
```

---

## Expected Output (Phase 0)

```
  +------------------------------------------------+
  |                                                |
  |        H E L I O S   H T T P   S E R V E R    |
  |        Production-grade C++17 HTTP Server      |
  |                                                |
  +------------------------------------------------+

[2026-09-24 00:49:52] [INFO ] [main] Config loaded from: config/server.conf
[2026-09-24 00:49:52] [INFO ] [main] ====================================================
[2026-09-24 00:49:52] [INFO ] [main]   Server name  : Helios HTTP Server
[2026-09-24 00:49:52] [INFO ] [main]   Version      : 0.1.0
[2026-09-24 00:49:52] [INFO ] [main]   Phase        : 0 — Project Foundation
[2026-09-24 00:49:52] [INFO ] [main]   Networking   : Not yet implemented (Phase 1)
[2026-09-24 00:49:52] [INFO ] [main]   Log level    : INFO
[2026-09-24 00:49:52] [INFO ] [main] ====================================================
[2026-09-24 00:49:52] [INFO ] [main] Phase 0 complete. Foundation is solid.
[2026-09-24 00:49:52] [INFO ] [main] Next: implement Phase 1 — Basic Single-Threaded TCP Server.
```

---

## Project Structure

```
helios/
├── CMakeLists.txt          Root build file
├── README.md               This file
├── .gitignore
├── docs/
│   ├── HLD.md              High-Level Design (all 14 phases)
│   ├── LLD.md              Low-Level Design (per-phase class designs)
│   ├── ROADMAP.md          14-phase roadmap with status
│   └── architecture/       Architecture diagrams (added per phase)
├── include/
│   └── helios/
│       ├── version.hpp     Version constants
│       ├── logger.hpp      Thread-safe logger (header-only)
│       └── config.hpp      INI config loader (header-only)
├── src/
│   ├── CMakeLists.txt
│   └── main.cpp            Entry point
├── tests/
│   ├── CMakeLists.txt
│   └── test_main.cpp       Test runner (Phase 0 placeholder)
├── benchmarks/             Load test scripts (Phase 7+)
├── config/
│   └── server.conf         Default server configuration
└── scripts/                Build/deploy helper scripts
```

---

## Configuration

Edit [`config/server.conf`](config/server.conf):

```ini
[server]
port           = 8080
worker_threads = 4

[logging]
level = INFO   # TRACE | DEBUG | INFO | WARN | ERROR | FATAL
```

---

## Design Principles

1. **Phase-by-phase** — every phase produces a running binary.
2. **No external dependencies** unless the trade-off is documented.
3. **RAII everywhere** — no raw owning pointers.
4. **Explicit error handling** — no silent failures.
5. **Graceful shutdown** — mandatory in every phase.
6. **Shared state is always identified and protected**.
7. **Every architectural decision has a documented reason**.

---

## Documentation

| Document | Description |
|----------|-------------|
| [HLD.md](docs/HLD.md) | High-Level Design: architecture, data flow, concurrency, scaling |
| [LLD.md](docs/LLD.md) | Low-Level Design: class diagrams, algorithms, API contracts |
| [ROADMAP.md](docs/ROADMAP.md) | 14-phase roadmap with status and learning goals |

---

## Roadmap

| Phase | Title | Status |
|-------|-------|--------|
| 0 | Project Foundation | ✅ |
| 1 | Basic Single-Threaded TCP Server | 🔲 |
| 2 | HTTP Abstraction Layer | 🔲 |
| 3 | Multithreaded Thread Pool | 🔲 |
| 4 | Routing and Application Layer | 🔲 |
| 5 | Connection Management | 🔲 |
| 6 | Logging and Observability | 🔲 |
| 7 | Benchmarking and Performance | 🔲 |
| 8 | Windows IOCP | 🔲 |
| 9 | In-Memory Cache | 🔲 |
| 10 | Database Integration | 🔲 |
| 11 | Rate Limiting | 🔲 |
| 12 | Security Hardening | 🔲 |
| 13 | Horizontal Scaling | 🔲 |
| 14 | Production Architecture Review | 🔲 |

---

## Contributing / Learning

This project is intentionally developed one phase at a time. If you are following along:

1. Read the HLD and LLD for the phase **before** looking at the code.
2. Try to implement the phase yourself, then compare.
3. Read every comment — they explain *why*, not just *what*.
4. Run the tests after every change.

---

## License

MIT License — see `LICENSE` (to be added).

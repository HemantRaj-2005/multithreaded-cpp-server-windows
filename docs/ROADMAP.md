# Helios HTTP Server — Roadmap

A 14-phase journey from "blank project" to production-grade multithreaded HTTP server.

Each phase builds on the previous. Every phase produces a **running binary** — we never break the build.

---

## Phase Status Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Complete |
| 🔄 | In Progress |
| 🔲 | Planned |

---

## Phases

### ✅ Phase 0 — Project Foundation
**Goal:** Establish the project structure, build system, logging, and configuration.

- [x] CMake build system (MSVC + MinGW)
- [x] Project directory structure
- [x] `helios::Logger` — thread-safe, header-only, six log levels
- [x] `helios::Config` — INI-style config loader with defaults
- [x] `helios::VERSION_*` constants
- [x] `.gitignore`
- [x] `README.md`, `HLD.md`, `LLD.md`, `ROADMAP.md`
- [x] Placeholder test runner

**Key learning:** CMake, project organisation, logging design, configuration patterns.

---

### 🔲 Phase 1 — Basic Single-Threaded TCP Server
**Goal:** Accept a TCP connection and return an HTTP response. No concurrency.

Deliverables:
- `TcpServer` class (create socket, bind, listen, accept)
- `TcpConnection` RAII wrapper
- Raw HTTP response: `HTTP/1.1 200 OK\r\nContent-Length: ...\r\n\r\nHello`
- Winsock error handling
- Graceful `Ctrl+C` shutdown

**Key learning:** TCP socket programming, Winsock API, HTTP wire format.

---

### 🔲 Phase 2 — HTTP Abstraction Layer
**Goal:** Separate networking from HTTP protocol handling.

Deliverables:
- `HttpParser` — state machine for request line, headers, body
- `HttpRequest` value object
- `HttpResponse` builder
- `Router` with static routes
- Routes: `GET /`, `GET /health`, `GET /hello`

**Key learning:** HTTP/1.1 protocol, parser design, routing, separation of concerns.

---

### 🔲 Phase 3 — Multithreaded Server (Thread Pool)
**Goal:** Handle concurrent connections via a worker thread pool.

Deliverables:
- `WorkQueue<T>` — mutex + condition_variable
- `ThreadPool` — N worker threads
- Accept thread feeds the queue
- Workers process HTTP requests
- Thread-safe Logger verified under concurrency
- Graceful shutdown (drain queue, join threads)

**Key learning:** Threads vs. processes, context switching, race conditions, mutex, condition variables, RAII lock guards.

---

### 🔲 Phase 4 — Routing and Application Layer
**Goal:** Clean application layer with RESTful routing.

Deliverables:
- Route registration API: `server.get("/users", handler)`
- Path parameter extraction: `/users/{id}`
- Method routing: GET, POST, PUT, DELETE
- Middleware pipeline concept
- Example endpoints: `GET /health`, `GET /users`, `GET /users/{id}`, `POST /users`

**Key learning:** REST design, handler patterns, middleware, separation of application logic.

---

### 🔲 Phase 5 — Connection Management
**Goal:** Production-quality connection lifecycle management.

Deliverables:
- HTTP Keep-Alive (persistent connections)
- Read/write/connection timeouts
- Maximum simultaneous connections
- Maximum request/header size limits
- Config-driven: all limits in `server.conf`

**Key learning:** HTTP/1.1 keep-alive, socket timeouts, DoS prevention basics.

---

### 🔲 Phase 6 — Logging and Observability
**Goal:** Production-grade structured logging and metrics.

Deliverables:
- Access log: `[INFO] GET /users 200 12ms`
- Error log with stack context
- Millisecond timestamp precision
- Metrics: requests/sec, active connections, P50/P95/P99 latency, worker utilisation
- Optional: file log sink

**Key learning:** Structured logging, metrics design, observability patterns.

---

### 🔲 Phase 7 — Benchmarking and Performance Analysis
**Goal:** Measure and understand the performance characteristics of each architecture.

Deliverables:
- Benchmark scripts (`wrk`, `ab`, PowerShell)
- Results: throughput (req/s), P50/P95/P99 latency, CPU/memory
- Comparison table: single-threaded vs. thread-per-conn vs. thread pool
- Written analysis explaining *why* the numbers look the way they do

**Key learning:** Benchmarking methodology, Amdahl's Law, context-switch overhead, memory bandwidth.

---

### 🔲 Phase 8 — Windows IOCP
**Goal:** Replace blocking I/O with asynchronous I/O via Windows I/O Completion Ports.

Deliverables:
- `CreateIoCompletionPort` integration
- Overlapped `WSARecv` / `WSASend`
- IOCP worker thread pool
- Benchmark: IOCP vs. blocking thread pool

**Key learning:** I/O multiplexing, async I/O, completion ports, IOCP architecture, scalability at C10K+.

---

### 🔲 Phase 9 — In-Memory Cache
**Goal:** Reduce backend load with a thread-safe LRU cache with TTL.

Deliverables:
- `Cache<K, V>` — LRU eviction, TTL per entry
- Thread-safe via `std::shared_mutex`
- Cache-aside pattern integrated with handlers
- Cache hit/miss metrics

**Key learning:** Cache design, LRU implementation (`std::list` + `std::unordered_map`), shared_mutex for read-heavy workloads.

---

### 🔲 Phase 10 — Database Integration
**Goal:** Back the API with real persistent storage.

Deliverables:
- PostgreSQL integration (libpq or equivalent)
- Connection pool
- User CRUD: `GET /users/{id}`, `POST /users`
- Prepared statements
- Transaction handling

**Key learning:** Database connection pooling, query latency, prepared statements, N+1 problem.

---

### 🔲 Phase 11 — Rate Limiting
**Goal:** Protect the server from excessive traffic.

Deliverables:
- Token bucket algorithm per IP
- Config-driven: `requests_per_sec`
- `429 Too Many Requests` response
- Algorithm comparison: fixed window vs. sliding window vs. token bucket

**Key learning:** Rate limiting algorithms, time-based state management.

---

### 🔲 Phase 12 — Security Hardening
**Goal:** Apply production security practices.

Deliverables:
- Request size limits
- Header count/size limits
- Input validation framework
- Malformed request rejection (400/413/431)
- Protection against HTTP request smuggling basics
- Timeout-based DoS prevention

**Key learning:** Web security fundamentals, input validation, secure coding in C++.

---

### 🔲 Phase 13 — Horizontal Scaling Design
**Goal:** Design and document how Helios scales across multiple nodes.

Deliverables:
- Architecture diagram: Load Balancer → N Helios nodes → Cache → DB
- Load balancing strategies: round-robin, least-connections, IP hash
- Session management for stateless servers
- Health check endpoint (`GET /health`)
- Documented trade-offs

**Key learning:** Horizontal scaling, stateless design, CAP theorem basics, consistent hashing.

---

### 🔲 Phase 14 — Production Architecture Review
**Goal:** Combine all phases into a complete production architecture.

Deliverables:
- Final architecture diagram (full system)
- Production deployment checklist
- Scalability analysis
- Reliability and fault-tolerance discussion
- Security audit checklist
- Observability completeness review
- Written "what we would do next" section

**Key learning:** Production engineering, system design interviews, trade-off documentation.

---

## Technology Decisions Summary

| Area | Technology | Introduced |
|------|-----------|-----------|
| Language | C++17 | Phase 0 |
| Build | CMake 3.20+ | Phase 0 |
| Logging | Hand-rolled, header-only | Phase 0 |
| Networking | Winsock2 | Phase 1 |
| Concurrency | `std::thread` + mutex | Phase 3 |
| Async I/O | Windows IOCP | Phase 8 |
| Database | PostgreSQL (libpq) | Phase 10 |
| Load testing | wrk / ab | Phase 7 |

---

*Last updated: Phase 0 — 2026-09-24*

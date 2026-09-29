#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/concurrency/thread_pool.hpp
//  ThreadPool — fixed-size pool of worker threads draining a WorkQueue.
//
//  Architecture
//  ────────────
//
//    Accept thread (TcpServer::run)
//         │
//         │  pool.submit(std::move(conn))
//         ▼
//    ┌─────────────────────────────────┐
//    │  WorkQueue<TcpConnection>       │  ← single shared queue
//    └─────────────────────────────────┘
//         │       │       │
//    ┌────▼──┐ ┌──▼──┐ ┌──▼──┐
//    │ W-0   │ │ W-1 │ │ W-N │   N = hardware_concurrency (or config)
//    └───────┘ └─────┘ └─────┘
//         │       │       │
//         └───────┴───────┘
//         handle_http_request(router, conn)
//
//  Why fixed-size vs. dynamic?
//    Fixed-size avoids unbounded thread creation under load.  Creating a new
//    thread for every request (thread-per-connection) is O(N) OS overhead and
//    fails at C10K.  A fixed pool amortises thread creation cost and gives
//    predictable resource usage.
//
//  Thread count heuristic: hardware_concurrency()
//    std::thread::hardware_concurrency() returns the number of logical CPU
//    cores.  For I/O-bound workloads, 2× or 4× is common; we start with 1×
//    and document the trade-off.  Phase 7 benchmarks will inform the optimal
//    multiplier.  The count is also readable from config.
//
//  Shutdown protocol (RAII)
//  ─────────────────────────
//    ~ThreadPool() calls stop() which:
//      1. Calls WorkQueue::stop() → wakes all workers.
//      2. Joins all worker threads (blocks until each finishes its current task).
//    This guarantees no worker thread outlives the ThreadPool object.
//
//  Move semantics of TcpConnection
//    Workers receive TcpConnection by value (moved from the queue).
//    When the handler returns, conn's destructor fires → socket closed.
//    No explicit cleanup required in the worker loop.
//
//  Exception safety in workers
//    Each worker wraps handler invocation in try/catch.
//    An unhandled exception must not kill a worker thread — that would
//    permanently reduce the pool size with no recovery path.
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/concurrency/work_queue.hpp"
#include "helios/net/tcp_connection.hpp"

#include <functional>
#include <thread>
#include <vector>

namespace helios::concurrency {

// The handler type workers call per connection.
// Matches TcpServer::ConnectionHandler (same signature).
using ConnectionHandler =
    std::function<void(helios::net::TcpConnection)>;

class ThreadPool {
public:
    // Construct a pool of `num_threads` worker threads.
    // `handler` is the function each worker calls per dequeued connection.
    // Precondition: `num_threads` > 0.
    explicit ThreadPool(unsigned int    num_threads,
                        ConnectionHandler handler);

    // Graceful shutdown:
    //   1. Stop the work queue (wakes all blocked workers).
    //   2. Join every worker thread.
    // RAII: no manual stop() needed by the caller.
    ~ThreadPool();

    // Non-copyable, non-movable.
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    // ── Producer API ──────────────────────────────────────────────────────

    // Enqueue a connection for a worker to process.
    // Called from the accept thread (TcpServer::run).
    // TcpConnection is non-copyable → must be moved.
    void submit(helios::net::TcpConnection conn);

    // ── Status ────────────────────────────────────────────────────────────
    unsigned int thread_count() const noexcept { return thread_count_; }
    std::size_t  queue_size()   const          { return queue_.size(); }

private:
    // The body of each worker thread.
    // Loops: pop from queue → handle → repeat.
    // Exits when pop() returns nullopt (queue stopped + empty).
    void worker_loop();

    unsigned int                              thread_count_;
    ConnectionHandler                         handler_;
    WorkQueue<helios::net::TcpConnection>     queue_;
    std::vector<std::thread>                  threads_;
};

} // namespace helios::concurrency

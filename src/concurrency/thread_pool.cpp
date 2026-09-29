// ─────────────────────────────────────────────────────────────────────────────
//  src/concurrency/thread_pool.cpp
//  ThreadPool implementation.
// ─────────────────────────────────────────────────────────────────────────────

#include "helios/concurrency/thread_pool.hpp"
#include "helios/logger.hpp"

#include <algorithm>   // std::max
#include <string>

namespace helios::concurrency {

// ── Constructor ───────────────────────────────────────────────────────────────

ThreadPool::ThreadPool(unsigned int num_threads, ConnectionHandler handler)
    : thread_count_{std::max(1u, num_threads)}
    , handler_     {std::move(handler)}
{
    // Reserve capacity so push_back below doesn't reallocate and invalidate
    // references (not an issue here, but good practice).
    threads_.reserve(thread_count_);

    // Spawn worker threads.
    // Each thread runs worker_loop() until the queue is stopped.
    //
    // Why &ThreadPool::worker_loop instead of a lambda?
    //   A member-function pointer + this is slightly cleaner than capturing
    //   `this` in a lambda for a long-lived thread body.  Both are equivalent.
    for (unsigned int i = 0; i < thread_count_; ++i) {
        threads_.emplace_back(&ThreadPool::worker_loop, this);
    }

    LOG_INFO("ThreadPool",
             "Started " + std::to_string(thread_count_) + " worker thread(s)");
}

// ── Destructor ────────────────────────────────────────────────────────────────

ThreadPool::~ThreadPool() {
    // Step 1: Tell the queue to stop accepting new work and wake all workers.
    queue_.stop();

    // Step 2: Join every worker thread.
    // join() blocks until the thread's function returns.
    // This guarantees no thread outlives the ThreadPool object.
    //
    // Why must we join before the destructor returns?
    //   After ~ThreadPool() returns, handler_ and queue_ are destroyed.
    //   If any worker is still running and tries to access them, it causes
    //   undefined behaviour (use-after-destroy).  join() prevents this.
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }

    LOG_INFO("ThreadPool", "All worker threads joined — pool destroyed");
}

// ── submit() ──────────────────────────────────────────────────────────────────

void ThreadPool::submit(helios::net::TcpConnection conn) {
    // Move the connection into the queue.
    // TcpConnection is non-copyable, so std::move is mandatory.
    // After this call, `conn` is in a moved-from state (fd_ == INVALID_SOCKET).
    queue_.push(std::move(conn));
}

// ── worker_loop() ─────────────────────────────────────────────────────────────
//
//  Each worker thread runs this loop indefinitely until pop() returns nullopt.
//
//  The loop structure:
//
//    while (true) {
//        auto maybe_conn = queue_.pop();     // BLOCKS until work or stop
//        if (!maybe_conn) break;             // queue stopped → exit
//        handler_(std::move(*maybe_conn));   // process the connection
//    }
//
//  Exception handling:
//    If handler_ throws (e.g. because a route handler has a bug), we must NOT
//    let the exception propagate out of the thread function.
//    An uncaught exception in a std::thread terminates the entire process
//    (std::terminate is called).  We log and continue to keep the pool alive.

void ThreadPool::worker_loop() {
    while (true) {
        // pop() blocks here until either:
        //   (a) a TcpConnection is pushed onto the queue, or
        //   (b) queue_.stop() is called (returns nullopt)
        auto maybe_conn = queue_.pop();

        if (!maybe_conn) {
            // Received the shutdown signal — exit the loop.
            break;
        }

        // Process the connection.
        // std::move transfers ownership from the optional into the handler.
        // When handler_ returns, the TcpConnection destructor closes the socket.
        try {
            handler_(std::move(*maybe_conn));
        } catch (const std::exception& ex) {
            LOG_ERROR("ThreadPool",
                      std::string("Worker caught exception: ") + ex.what());
        } catch (...) {
            LOG_ERROR("ThreadPool", "Worker caught unknown exception");
        }
    }
    // Worker exits here.  The thread function returns, marking the thread
    // as finished.  ThreadPool::~ThreadPool()'s join() will then unblock.
}

} // namespace helios::concurrency

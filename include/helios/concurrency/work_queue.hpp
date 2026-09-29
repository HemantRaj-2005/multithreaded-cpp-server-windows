#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  helios/concurrency/work_queue.hpp
//  WorkQueue<T> — a thread-safe, blocking, bounded-optional MPSC/MPMC queue.
//
//  Used in Phase 3 to pass TcpConnection objects from the accept thread to
//  the worker thread pool.
//
//  Design: mutex + condition_variable + std::deque
//  ─────────────────────────────────────────────
//  This is the canonical C++ concurrent queue implementation.  We choose it
//  over lock-free alternatives because:
//
//    1. Correctness first: a mutex-protected deque is provably correct and
//       easy to inspect.  Lock-free queues have subtle ABA-problem failure
//       modes and require explicit memory-order reasoning.
//
//    2. For a thread pool with N ≤ 64 workers, lock contention on the queue
//       is negligible compared to the cost of each HTTP request handler.
//       Profiling (Phase 7) will tell us if this needs revisiting.
//
//    3. std::condition_variable provides efficient OS-level blocking —
//       sleeping workers consume zero CPU while waiting for work.
//
//  Shutdown protocol: poison-pill via stop()
//  ─────────────────────────────────────────
//  stop() sets stopped_ = true and notifies ALL waiting threads.
//  Workers wake up, see stopped_ == true, and exit their loops.
//
//  Why not a poison-pill sentinel value?
//    A sentinel (e.g. nullptr) requires T to have a "null" representation.
//    Using stopped_ keeps the queue generic and avoids sentinel coupling.
//
//  Bounded vs. unbounded:
//    Phase 3: unbounded — the queue grows to accommodate bursts.
//    Phase 5: we will add a max_size cap to prevent memory exhaustion under
//    extreme load, returning an error or blocking the accept thread.
//
//  Template requirement on T:
//    T must be movable (non-copyable is fine — TcpConnection is non-copyable).
//    push() takes T by value (already moved in by the caller).
//    pop() returns std::optional<T>:
//      • contains a value if work was dequeued successfully
//      • empty (nullopt) if the queue was stopped before an item arrived
// ─────────────────────────────────────────────────────────────────────────────

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace helios::concurrency {

template <typename T>
class WorkQueue {
public:
    WorkQueue() = default;

    // Non-copyable, non-movable (owns synchronisation primitives).
    WorkQueue(const WorkQueue&)            = delete;
    WorkQueue& operator=(const WorkQueue&) = delete;
    WorkQueue(WorkQueue&&)                 = delete;
    WorkQueue& operator=(WorkQueue&&)      = delete;

    // ── Producer side ─────────────────────────────────────────────────────

    // Push one item of work onto the back of the queue.
    // Notifies ONE waiting worker thread (via notify_one).
    //
    // Why notify_one and not notify_all?
    //   notify_all would wake every sleeping worker to compete for the single
    //   new item — the "thundering herd" problem.  Only one wins; the rest go
    //   back to sleep, wasting context-switch budget.
    //   notify_one wakes exactly one worker, which is sufficient.
    //
    // If stop() has already been called, push() silently discards the item.
    // This prevents the accept thread from enqueuing work that no worker
    // will ever consume.
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            if (stopped_) return;            // discard after shutdown
            queue_.push_back(std::move(item));
        }
        // Notify OUTSIDE the lock.
        // Notifying while holding the lock is legal but wasteful:
        // the woken thread immediately tries to acquire the same lock we
        // still hold, causing one guaranteed unnecessary context switch.
        cv_.notify_one();
    }

    // ── Consumer side ─────────────────────────────────────────────────────

    // Blocking pop.  Blocks the caller until:
    //   (a) an item is available → returns std::optional<T> containing it, or
    //   (b) stop() is called    → returns std::nullopt
    //
    // Usage in worker threads:
    //   while (auto item = queue.pop()) {
    //       process(*item);
    //   }
    //   // pop() returned nullopt → queue stopped → thread exits
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock{mutex_};

        // wait() atomically:
        //   1. Releases the lock and suspends the thread (efficient OS sleep).
        //   2. Re-acquires the lock when notified.
        //   3. Evaluates the predicate; if false, goes back to sleep.
        //      (Handles spurious wakeups — real OS quirk, not theoretical.)
        cv_.wait(lock, [this] {
            return !queue_.empty() || stopped_;
        });

        if (queue_.empty()) {
            // stopped_ is true and the queue is drained → signal thread exit
            return std::nullopt;
        }

        // Move the front item out of the deque.
        // std::move is essential here: T may be non-copyable (TcpConnection).
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    // ── Shutdown ──────────────────────────────────────────────────────────

    // Signal all waiting threads to wake up and return nullopt from pop().
    // After stop() is called:
    //   • Existing items in the queue are DRAINED by workers before they exit.
    //     Wait — actually workers check stopped_ AFTER checking for items,
    //     so remaining items will be processed as long as workers loop.
    //     In our ThreadPool implementation, workers exit as soon as pop()
    //     returns nullopt (i.e. queue empty + stopped).  Items still in the
    //     queue when stop() is called may or may not be processed depending
    //     on timing.  For Phase 3 (Ctrl+C shutdown), this is acceptable.
    //     Phase 5 will add a drain-before-stop mode.
    //   • New push() calls are silently discarded.
    void stop() {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            stopped_ = true;
        }
        // Wake ALL threads: every worker must exit, not just one.
        cv_.notify_all();
    }

    // ── Status (for tests / logging) ──────────────────────────────────────

    bool is_stopped() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return stopped_;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return queue_.empty();
    }

private:
    // All three members are protected by mutex_.
    // The mutex_ is mutable so that const status queries (size, empty, is_stopped)
    // can also acquire the lock without casting away const.
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::deque<T>           queue_;
    bool                    stopped_{false};
};

} // namespace helios::concurrency

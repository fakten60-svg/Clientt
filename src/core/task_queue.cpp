// ============================================================================
//  woke.wtf — src/core/task_queue.cpp
//  Task queue implementation. The ring is fixed-size, drain() swaps jobs out
//  under the lock and runs them outside it, and the game thread is identified
//  by a thread-local flag so post() can short-circuit into a direct call.
// ============================================================================
#include "core/task_queue.hpp"

#include <utility>

#include "core/logger.hpp"

namespace woke::core {

namespace {

thread_local bool t_on_game_thread = false;

} // namespace

task_queue& task_queue::instance() {
    static task_queue queue;
    return queue;
}

void task_queue::mark_game_thread() {
    if (!t_on_game_thread) {
        t_on_game_thread = true;
        WOKE_DEBUG("core", "task queue bound to the game thread (drains per present)");
    }
}

bool task_queue::on_game_thread() const {
    return t_on_game_thread;
}

bool task_queue::post(job j) {
    if (!j) {
        return false;
    }
    // On the game thread run immediately — unless we are inside drain(), in
    // which case queueing keeps the current job from recursing into the loop.
    if (t_on_game_thread && !draining_.load(std::memory_order_acquire)) {
        posted_.fetch_add(1, std::memory_order_relaxed);
        executed_.fetch_add(1, std::memory_order_relaxed);
        j();
        return true;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ >= capacity()) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        WOKE_WARN("core", "task queue full (%zu) — job dropped", capacity());
        return false;
    }
    slots_[(head_ + count_) % capacity()] = std::move(j);
    ++count_;
    posted_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

std::size_t task_queue::drain() {
    std::array<job, capacity()> local{};
    std::size_t taken = 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (count_ > 0 && taken < capacity()) {
            local[taken++] = std::move(slots_[head_]);
            slots_[head_] = nullptr;
            head_ = (head_ + 1) % capacity();
            --count_;
        }
    }
    if (taken == 0) {
        return 0;
    }

    draining_.store(true, std::memory_order_release);
    for (std::size_t i = 0; i < taken; ++i) {
        if (local[i]) {
            local[i]();
            executed_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    draining_.store(false, std::memory_order_release);
    return taken;
}

std::size_t task_queue::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

long long task_queue::posted_count() const {
    return posted_.load(std::memory_order_relaxed);
}

long long task_queue::executed_count() const {
    return executed_.load(std::memory_order_relaxed);
}

long long task_queue::dropped_count() const {
    return dropped_.load(std::memory_order_relaxed);
}

void task_queue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < capacity(); ++i) {
        slots_[i] = nullptr;
    }
    head_ = 0;
    count_ = 0;
}

} // namespace woke::core

// ============================================================================
//  woke.wtf — src/core/task_queue.hpp
//  Main-thread task queue.
//
//  Anything that may touch the game object graph from a foreign thread (the
//  deferred-init worker, config reloads, keybind edits, test exports) posts a
//  job here instead of calling JNI directly. The present detour runs on the
//  game's frame thread and drains the queue once per presented frame, so all
//  game-state mutation happens on one thread — the JNI equivalent of
//  MinecraftClient#execute(), and the reason a toggle can never race a frame.
//
//  Storage is a fixed-capacity ring of jobs: posting after warm-up performs no
//  heap allocation, and drain() moves jobs out under the lock before running
//  them, so no job can ever run while the queue is locked.
// ============================================================================
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>

namespace woke::core {

class task_queue {
public:
    using job = std::function<void()>;

    // Capacity of the ring (also the per-drain bound).
    static constexpr std::size_t kCapacity = 256;
    static constexpr std::size_t capacity() { return kCapacity; }

    static task_queue& instance();

    // Enqueues a job. From the game thread the job runs inline (ordering
    // preserved); from any other thread it is deferred to the next drain().
    // Returns false when the queue is full — the job is dropped and counted.
    bool post(job j);

    // Marks the calling thread as the game/frame thread.
    void mark_game_thread();
    bool on_game_thread() const;

    // Runs every queued job on the calling thread. Returns how many ran.
    std::size_t drain();

    std::size_t pending() const;
    long long posted_count() const;
    long long executed_count() const;
    long long dropped_count() const;

    // Drops queued jobs without running them (shutdown path).
    void clear();

private:
    task_queue() = default;

    mutable std::mutex mutex_;
    std::array<job, kCapacity> slots_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    std::atomic<bool> draining_{false};
    std::atomic<long long> posted_{0};
    std::atomic<long long> executed_{0};
    std::atomic<long long> dropped_{0};
};

} // namespace woke::core

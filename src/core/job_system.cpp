#include "core/job_system.hpp"

#include <algorithm>

namespace sc {

JobSystem::JobSystem(u32 thread_count) {
    if (thread_count == 0) {
        thread_count = std::max(1u, std::thread::hardware_concurrency());
    }
    workers_.reserve(thread_count);
    for (u32 i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

JobSystem::~JobSystem() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        ++generation_;
    }
    work_ready_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void JobSystem::worker_loop() {
    u64 seen_generation = 0;
    for (;;) {
        const std::function<void(u32)>* job = nullptr;
        u32 count = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_ready_.wait(lock, [&] { return stopping_ || generation_ != seen_generation; });
            if (stopping_) {
                return;
            }
            seen_generation = generation_;
            job = job_;
            count = job_count_;
        }

        if (job == nullptr) {
            continue;
        }

        for (;;) {
            const u32 index = next_.fetch_add(1, std::memory_order_relaxed);
            if (index >= count) {
                break;
            }
            (*job)(index);
            completed_.fetch_add(1, std::memory_order_release);
        }
        work_done_.notify_all();
    }
}

void JobSystem::parallel_for(u32 count, const std::function<void(u32)>& body) {
    if (count == 0) {
        return;
    }
    if (workers_.empty()) {
        for (u32 i = 0; i < count; ++i) {
            body(i);
        }
        return;
    }

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (job_ != nullptr) {
            // Nested dispatch degrades to serial work rather than deadlocking.
            for (u32 i = 0; i < count; ++i) {
                body(i);
            }
            return;
        }
        job_ = &body;
        job_count_ = count;
        next_.store(0, std::memory_order_relaxed);
        completed_.store(0, std::memory_order_relaxed);
        ++generation_;
    }
    work_ready_.notify_all();

    for (;;) {
        const u32 index = next_.fetch_add(1, std::memory_order_relaxed);
        if (index >= count) {
            break;
        }
        body(index);
        completed_.fetch_add(1, std::memory_order_release);
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        work_done_.wait(lock, [&] {
            return completed_.load(std::memory_order_acquire) >= job_count_;
        });
        job_ = nullptr;
        job_count_ = 0;
    }
}

}  // namespace sc

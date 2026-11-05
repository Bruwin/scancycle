#pragma once
// A fixed worker pool used for meshing and any other embarrassingly parallel
// pass. Deliberately small: parallel_for with an index range, no task graph.
// Work items must be independent; the pool never guarantees ordering.
#include "core/types.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace sc {

class JobSystem {
public:
    explicit JobSystem(u32 thread_count = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Runs body(0..count-1) exactly once per index, spread over the pool plus
    // the calling thread. Returns when every index has completed.
    void parallel_for(u32 count, const std::function<void(u32)>& body);

    u32 thread_count() const { return static_cast<u32>(workers_.size()); }

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable work_ready_;
    std::condition_variable work_done_;

    const std::function<void(u32)>* job_ = nullptr;
    u32 job_count_ = 0;
    u64 generation_ = 0;
    bool stopping_ = false;

    std::atomic<u32> next_{0};
    std::atomic<u32> completed_{0};
};

}  // namespace sc

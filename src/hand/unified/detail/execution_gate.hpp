#pragma once

#include <linkerbot/hand/runtime/device_runtime.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>

namespace linkerbot::hand::unified_detail {
    // Composite transaction ownership. FIFO within each class, alternating
    // setters with waiting polling/active reads at transaction boundaries.
    // Native managers use this only when owned by a unified backend.
    class ExecutionGate : public std::enable_shared_from_this<ExecutionGate> {
    public:
        std::shared_ptr<void> acquire(bool command, runtime::Deadline deadline, runtime::CancellationToken token = {},
                                      std::function<bool()> valid = {}) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto &queue = command ? commands_ : reads_;
            const auto ticket = next_++;
            queue.push_back(ticket);
            try {
                for (;;) {
                    if (token.cancelled()) {
                        throw runtime::CancelledError("execution gate cancelled");
                    }
                    if (valid && !valid()) {
                        throw ClosedError("execution generation ended");
                    }
                    if (runtime::Clock::now() >= deadline) {
                        throw TimeoutError("execution gate deadline expired");
                    }
                    const bool turn = commands_.empty() || reads_.empty() || prefer_command_ == command;
                    if (!busy_ && queue.front() == ticket && turn) {
                        break;
                    }
                    changed_.wait_until(lock, std::min(deadline, runtime::Clock::now() + std::chrono::milliseconds{1}));
                }
                auto lease = std::shared_ptr<void>(this, [self = shared_from_this()](void *) {
                    const std::lock_guard<std::mutex> release(self->mutex_);
                    self->busy_ = false;
                    self->changed_.notify_all();
                });
                queue.pop_front();
                busy_ = true;
                prefer_command_ = !command;
                return lease;
            } catch (...) {
                queue.erase(std::find(queue.begin(), queue.end(), ticket));
                changed_.notify_all();
                throw;
            }
        }

    private:
        std::mutex mutex_;
        std::condition_variable changed_;
        std::deque<uint64_t> commands_;
        std::deque<uint64_t> reads_;
        uint64_t next_ = 0;
        bool busy_ = false;
        bool prefer_command_ = true;
    };
}

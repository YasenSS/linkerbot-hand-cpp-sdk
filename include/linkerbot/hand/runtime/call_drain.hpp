#pragma once
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
namespace linkerbot::hand::runtime {
    struct ChannelTestAccess;
    struct CallDrainTestAccess;
    // Tracks facade calls, including callers waiting for runtime admission. This is
    // lifetime accounting only: requests, response slots and all I/O live in Runtime.
    class CallDrain {
        friend struct ChannelTestAccess;
        friend struct CallDrainTestAccess;
        struct State {
            std::mutex mutex;
            std::condition_variable cv;
            size_t calls = 0;
            std::map<std::thread::id, size_t> owners;
        };

    public:
        class Guard {
        public:
            explicit Guard(std::shared_ptr<State> s) : state_(std::move(s)) {
                std::lock_guard<std::mutex> lock(state_->mutex);
                ++state_->owners[owner_];
                ++state_->calls;
                state_->cv.notify_all();
            }
            ~Guard() {
                std::lock_guard<std::mutex> lock(state_->mutex);
                --state_->calls;
                if (!--state_->owners[owner_]) {
                    state_->owners.erase(owner_);
                }
                state_->cv.notify_all();
            }
            Guard(const Guard &) = delete;
            Guard &operator=(const Guard &) = delete;

        private:
            std::shared_ptr<State> state_;
            std::thread::id owner_ = std::this_thread::get_id();
        };
        Guard enter() { return Guard(state_); }
        std::shared_ptr<void> hold() { return std::make_shared<Guard>(state_); }
        // A caller may explicitly close while retaining its own public Operation.
        // Drain other threads; its same-thread guards unwind after close returns.
        void drain() {
            std::unique_lock<std::mutex> lock(state_->mutex);
            state_->cv.wait(lock, [&] {
                const auto own = state_->owners.find(std::this_thread::get_id());
                return state_->calls == (own == state_->owners.end() ? 0 : own->second);
            });
        }

    private:
        std::shared_ptr<State> state_ = std::make_shared<State>();
    };
}

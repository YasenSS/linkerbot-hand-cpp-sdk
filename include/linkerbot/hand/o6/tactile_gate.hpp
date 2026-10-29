#pragma once
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/o6/legacy_request.hpp>

namespace linkerbot::hand::classic_detail {
    // A polling batch retains this reservation after its send call returns. Single
    // finger reads use the same admission, so they cannot consume a batch member.
    class TactileGate {
        friend struct AdmissionTestAccess;
        struct State {
            std::mutex mutex;
            std::condition_variable cv;
            size_t next = 0, active = 0, epoch = 0, leases = 0;
            bool stopped = false;
            Clock::time_point deadline{};
        };

    public:
        class Lease {
        public:
            Lease(std::shared_ptr<State> state, size_t id) : state_(std::move(state)), id_(id) {}
            ~Lease() {
                const std::scoped_lock lock(state_->mutex);
                if (state_->active == id_) {
                    state_->active = 0;
                }
                --state_->leases;
                state_->cv.notify_all();
            }
            size_t id() const { return id_; }

        private:
            std::shared_ptr<State> state_;
            size_t id_;
        };
        std::shared_ptr<Lease> acquire(Clock::time_point deadline, bool wait = true,
                                       const std::function<bool()> &cancelled = {}) {
            const auto call = calls_.enter();
            auto &s = *state_;
            std::unique_lock lock(s.mutex);
            const auto epoch = s.epoch;
            for (;;) {
                if (s.stopped || epoch != s.epoch || (cancelled && cancelled())) {
                    throw LifecycleError("tactile manager stopped");
                }
                const auto now = Clock::now();
                if (now >= deadline) {
                    throw internal::DataRelayTimeout("tactile admission deadline expired");
                }
                if (s.active && now >= s.deadline) {
                    s.active = 0;
                }
                if (!s.active) {
                    break;
                }
                if (!wait) {
                    return {};
                }
                s.cv.wait_until(lock, std::min(deadline, s.deadline));
            }
            auto lease = std::make_shared<Lease>(state_, ++s.next);
            ++s.leases;
            s.active = lease->id();
            s.deadline = deadline;
            return lease;
        }
        void notify() {
            const std::scoped_lock lock(state_->mutex);
            state_->cv.notify_all();
        }
        void cancel_pending() {
            const std::scoped_lock lock(state_->mutex);
            state_->stopped = true;
            ++state_->epoch;
            state_->cv.notify_all();
        }
        void stop() {
            cancel_pending();
            drain();
        }
        void drain() {
            if (callback_depth || can::CANDispatcher::is_any_receive_thread()) {
                return;
            }
            calls_.drain();
            std::unique_lock lock(state_->mutex);
            state_->cv.wait(lock, [&] { return state_->leases == 0; });
        }
        void resume() {
            const std::scoped_lock lock(state_->mutex);
            state_->stopped = false;
        }

    private:
        CallDrain calls_;
        std::shared_ptr<State> state_ = std::make_shared<State>();
    };
}

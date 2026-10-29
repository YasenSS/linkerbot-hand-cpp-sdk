#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/relay.hpp>
#include <linkerbot/transport/session.hpp>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace linkerbot::hand::l20::detail {
    using Clock = std::chrono::steady_clock;
    using Deadline = Clock::time_point;

    inline Deadline deadline_after(std::chrono::milliseconds timeout) {
        if (timeout.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        return Clock::now() + timeout;
    }

    inline bool classic_response(const can::Message &msg, uint32_t id, uint8_t length) {
        return msg.id == id && !msg.is_extended && !msg.is_fd && !msg.is_brs && msg.dlc == length;
    }

    // Only admission is serialized here; stop never waits for an operation's
    // lock. Its notification interrupts both queued callers and reply waiters.
    class Admission {
    public:
        class Guard {
        public:
            explicit Guard(Admission &owner) : owner_(owner) {}
            ~Guard() {
                const std::lock_guard lock(owner_.mutex_);
                owner_.busy_ = false;
                owner_.owner_ = {};
                --owner_.users_;
                owner_.cv_.notify_all();
            }
            Guard(const Guard &) = delete;
            Guard &operator=(const Guard &) = delete;

        private:
            Admission &owner_;
        };

        Guard enter(Deadline deadline, const std::function<bool()> &cancelled = {}) {
            if (can::CANDispatcher::is_any_receive_thread()) {
                throw LifecycleError("L20 synchronous operation is unavailable in receive callbacks");
            }
            std::unique_lock lock(mutex_);
            ++users_;
            try {
                while (busy_ && !stopped_) {
                    if (cancelled && cancelled()) {
                        throw LifecycleError("L20 manager stopped");
                    }
                    if (Clock::now() >= deadline) {
                        throw internal::DataRelayTimeout("L20 admission deadline expired");
                    }
                    cv_.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds{1}));
                }
                if (cancelled && cancelled()) {
                    throw LifecycleError("L20 manager stopped");
                }
                if (stopped_) {
                    throw ClosedError("L20 manager stopped");
                }
                if (Clock::now() >= deadline) {
                    throw internal::DataRelayTimeout("L20 admission deadline expired");
                }
                busy_ = true;
                owner_ = std::this_thread::get_id();
                return Guard(*this);
            } catch (...) {
                --users_;
                cv_.notify_all();
                throw;
            }
        }
        void drain() {
            if (can::CANDispatcher::is_any_receive_thread()) {
                return;
            }
            std::unique_lock lock(mutex_);
            // A sink may cancel its own synchronous operation. External stop
            // remains the barrier for that caller and all queued admissions.
            if (busy_ && owner_ == std::this_thread::get_id()) {
                return;
            }
            cv_.wait(lock, [this] { return users_ == 0; });
        }

        void stop() {
            const std::lock_guard lock(mutex_);
            stopped_ = true;
            cv_.notify_all();
        }
        [[nodiscard]] bool stopped() const { return stopped_.load(); }

    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        std::atomic<bool> stopped_{false};
        bool busy_ = false;
        size_t users_ = 0;
        std::thread::id owner_{};
    };

    // A legacy endpoint has no wire sequence. Any ambiguous completion poisons
    // the window until this object is rebuilt. stop/start never clears poison.
    struct Window {
        Admission admission;
        std::atomic<bool> polling_cancelled{false};
        std::mutex mutex;
        std::condition_variable cv;
        bool active = false;
        bool poisoned = false;
        bool solicited = false;
        Deadline deadline{};

        void check_locked() {
            if (active && Clock::now() >= deadline) {
                active = false;
                poisoned = true;
                cv.notify_all();
            }
            if (admission.stopped()) {
                throw ClosedError("L20 manager stopped");
            }
            if (poisoned) {
                throw DeviceNotReadyError("L20 reply window quarantined; rebuild manager after clearing the link");
            }
        }
        void await_idle(Deadline until) {
            std::unique_lock lock(mutex);
            check_locked();
            while (active) {
                cv.wait_until(lock, std::min(until, deadline));
                check_locked();
                if (Clock::now() >= until) {
                    throw internal::DataRelayTimeout("L20 previous reply window still active");
                }
            }
        }
        void begin_locked(Deadline until) {
            check_locked();
            if (active) {
                throw LifecycleError("L20 overlapping reply window");
            }
            deadline = until;
            active = true;
        }
        bool accepts_locked() {
            if (active && Clock::now() >= deadline) {
                active = false;
                poisoned = true;
                cv.notify_all();
            }
            return active && !poisoned && !admission.stopped();
        }
        void finish_locked() {
            active = false;
            cv.notify_all();
        }
        void poison_locked() {
            poisoned = true;
            finish_locked();
        }
        template <typename T> void stop(internal::DataRelay<T> &relay) {
            admission.stop();
            const std::lock_guard lock(mutex);
            if (active) {
                poison_locked();
            }
            relay.cancel(std::make_exception_ptr(ClosedError("L20 manager stopped")));
            cv.notify_all();
        }
    };

    // Leave the active window in place until publication completes. A new
    // request cannot register a waiter for this sample while a sink is running;
    // stop/timeout may poison the window, but cannot reopen it.
    template <typename T>
    void publish(Window &window, internal::DataRelay<T> &relay, const T &data, std::unique_lock<std::mutex> &lock) {
        const bool solicited = window.solicited;
        lock.unlock();
        try {
            if (solicited) {
                (void)relay.push_if_waiting(data);
            } else {
                relay.push(data);
            }
        } catch (...) {
            lock.lock();
            window.poison_locked();
            throw;
        }
        lock.lock();
        window.finish_locked();
    }

    inline void send(transport::Session &session, const std::vector<can::Message> &frames, Deadline deadline,
                     std::chrono::microseconds gap = {}, const std::function<bool()> &cancelled = {}) {
        transport::CancellationSource cancellation;
        transport::SendOptions options;
        options.deadline = deadline;
        options.cancellation = cancellation.token();
        try {
            const auto receipt = session.submit({frames, gap}, options);
            while (!receipt.completed()) {
                if (cancelled && cancelled()) {
                    throw LifecycleError("L20 send cancelled; claimed backend I/O may still complete");
                }
                if (Clock::now() >= deadline) {
                    throw internal::DataRelayTimeout(
                        "L20 send deadline expired; claimed backend I/O may still complete");
                }
                try {
                    (void)receipt.wait_until(std::min(deadline, Clock::now() + std::chrono::milliseconds{1}));
                } catch (const can::TimeoutError &) { /* recheck cancellation and total deadline */
                }
            }
            const auto result = receipt.result();
            if (result.error) {
                try {
                    std::rethrow_exception(result.error);
                } catch (const can::TimeoutError &error) {
                    throw TimeoutError(std::string(error.what()) + (result.claimed_at
                                                                        ? "; backend I/O may have completed"
                                                                        : "; batch was not claimed"),
                                       {}, std::current_exception());
                }
            }
            if (result.outcome != transport::SendOutcome::dispatched) {
                throw TransportError("L20 batch was not dispatched");
            }
            if (Clock::now() >= deadline) {
                throw internal::DataRelayTimeout("L20 send exceeded request deadline; backend I/O may have completed");
            }
            if (cancelled && cancelled()) {
                throw LifecycleError("L20 send cancelled");
            }
        } catch (...) {
            cancellation.cancel(); // also stops unsent frames of a claimed batch
            throw;
        }
    }

    inline void send(can::CANDispatcher &dispatcher, const std::vector<can::Message> &frames, Deadline deadline,
                     std::chrono::microseconds gap = {}, const std::function<bool()> &cancelled = {}) {
        size_t sent = 0;
        bool uncertain = false;
        dispatcher.send_batch_until(frames, deadline, gap, {}, cancelled, sent, uncertain);
        // A synchronous backend cannot be preempted. Do not report a reply as
        // on-time if that backend consumed the caller's remaining budget.
        if (Clock::now() >= deadline) {
            throw internal::DataRelayTimeout("L20 send exceeded request deadline; backend I/O may have completed");
        }
    }
}

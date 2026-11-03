#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <linkerbot/hand/errors.hpp>

namespace linkerbot::internal {
    class DataRelayTimeout : public ::linkerbot::hand::TimeoutError {
    public:
        using ::linkerbot::hand::TimeoutError::TimeoutError;
    };

    template <typename T> class DataRelay {
        using Clock = std::chrono::steady_clock;
        using Deadline = Clock::time_point;
        struct Waiter {
            Deadline deadline;
            std::condition_variable cv;
            std::optional<T> data;
            std::exception_ptr error;
        };
        struct Sink {
            explicit Sink(std::function<void(const T &)> fn) : callback(std::move(fn)) {}
            std::function<void(const T &)> callback;
            std::mutex mutex;
            std::condition_variable cv;
            bool active = true;
            size_t running = 0;
        };
        struct Invocation {
            Sink *sink;
            const DataRelay *owner;
            Invocation *previous;
        };
        inline static thread_local Invocation *invocation_ = nullptr;

        // Admission is separate from waiter registration, but shares its lock
        // and cancellation epoch. A cancel/resume cannot revive a queued call.
        class Admission {
        public:
            Admission(DataRelay &owner, Deadline deadline) : owner_(owner) {
                std::unique_lock<std::mutex> lock(owner_.mutex_);
                const auto epoch = owner_.epoch_;
                if (!owner_.admission_cv_.wait_until(lock, deadline, [&] {
                        return !owner_.request_active_ || owner_.cancelled_ || owner_.epoch_ != epoch;
                    })) {
                    throw DataRelayTimeout("request deadline expired during admission");
                }
                if (owner_.cancelled_ || owner_.epoch_ != epoch) {
                    throw ::linkerbot::hand::LifecycleError("cancelled");
                }
                if (Clock::now() >= deadline) {
                    throw DataRelayTimeout("request deadline expired before send");
                }
                owner_.request_active_ = true;
                epoch_ = epoch;
            }
            ~Admission() {
                std::lock_guard<std::mutex> lock(owner_.mutex_);
                owner_.request_active_ = false;
                owner_.admission_cv_.notify_all();
            }
            size_t epoch() const { return epoch_; }

        private:
            DataRelay &owner_;
            size_t epoch_ = 0;
        };

    public:
        DataRelay() = default;
        ~DataRelay() {
            try {
                set_sink({});
                // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
                // NOLINTNEXTLINE(bugprone-empty-catch)
            } catch (...) {
            }
        }
        DataRelay(const DataRelay &) = delete;
        DataRelay &operator=(const DataRelay &) = delete;
        DataRelay(DataRelay &&) = delete;
        DataRelay &operator=(DataRelay &&) = delete;

        std::optional<T> snapshot() const {
            const std::lock_guard<std::mutex> lock(mutex_);
            return latest_;
        }

        T wait(float timeout_s) {
            const auto deadline = deadline_after(timeout_s);
            return wait_until(register_waiter(deadline), deadline);
        }

        template <typename Send> T request(Send &&send, float timeout_s) {
            return request_until(std::forward<Send>(send), deadline_after(timeout_s));
        }

        template <typename Send> T request_until(Send &&send, Deadline deadline) {
            Admission admission(*this, deadline);
            auto waiter = register_waiter(deadline, admission.epoch());
            try {
                std::forward<Send>(send)();
            } catch (...) {
                discard_waiter(waiter);
                throw;
            }
            // An early reply does not turn a send that overran its budget into
            // an on-time success. A backend already entered may be uncertain.
            if (Clock::now() >= deadline) {
                discard_waiter(waiter);
                throw DataRelayTimeout("request deadline expired during send; backend I/O may have completed");
            }
            return wait_until(waiter, deadline);
        }

        void push(const T &data) { (void)push_impl(data, false); }
        bool push_if_waiting(const T &data) { return push_impl(data, true); }

        void fail(std::exception_ptr error) {
            if (!error) {
                throw std::invalid_argument("DataRelay::fail requires an exception");
            }
            const std::lock_guard<std::mutex> lock(mutex_);
            fail_locked(error);
        }

        // Disable old copies before returning; wait for callbacks already
        // executing, without holding the relay lock across user code. Keep
        // self-cancelled slots reachable until an external barrier completes.
        void set_sink(std::function<void(const T &)> sink) {
            auto replacement = sink ? std::make_shared<Sink>(std::move(sink)) : nullptr;
            std::vector<std::shared_ptr<Sink>> old;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                old = retired_sinks_;
                if (sink_) {
                    old.push_back(sink_);
                }
                retired_sinks_ = old;
                sink_ = std::move(replacement);
                for (const auto &slot : old) {
                    const std::lock_guard<std::mutex> slot_lock(slot->mutex);
                    slot->active = false;
                }
            }
            // Two generations may execute on different threads. Neither
            // callback may wait on the other while both cancel this relay.
            for (auto *call = invocation_; call; call = call->previous) {
                if (call->owner == this) {
                    return;
                }
            }
            for (const auto &slot : old) {
                {
                    std::unique_lock<std::mutex> lock(slot->mutex);
                    slot->cv.wait(lock, [&] { return slot->running == 0; });
                }
                const std::lock_guard<std::mutex> lock(mutex_);
                retired_sinks_.erase(std::remove(retired_sinks_.begin(), retired_sinks_.end(), slot),
                                     retired_sinks_.end());
            }
        }

        [[nodiscard]] size_t pending_count() const {
            const std::lock_guard<std::mutex> lock(mutex_);
            return waiters_.size();
        }

        template <typename Fn> decltype(std::declval<Fn &>()()) exclusive(Fn &&fn) {
            Admission admission(*this, Deadline::max());
            return std::forward<Fn>(fn)();
        }

        void cancel(std::exception_ptr error) {
            if (!error) {
                throw std::invalid_argument("DataRelay::cancel requires an exception");
            }
            const std::lock_guard<std::mutex> lock(mutex_);
            cancelled_ = true;
            ++epoch_;
            fail_locked(error);
            admission_cv_.notify_all();
        }

        void resume() {
            const std::lock_guard<std::mutex> lock(mutex_);
            cancelled_ = false;
            admission_cv_.notify_all();
        }

    private:
        static Deadline deadline_after(float seconds) {
            const auto now = Clock::now();
            if (!std::isfinite(seconds) || seconds <= 0 ||
                static_cast<long double>(seconds) >=
                    std::chrono::duration<long double>(Deadline::max() - now).count()) {
                throw std::invalid_argument("timeout must be positive, finite and representable");
            }
            return now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(seconds));
        }
        void fail_locked(const std::exception_ptr &error) {
            for (const auto &waiter : waiters_) {
                waiter->error = error;
                waiter->cv.notify_all();
            }
            waiters_.clear();
        }
        bool push_impl(const T &data, bool require_waiter) {
            std::shared_ptr<Sink> sink;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (cancelled_) {
                    return false;
                }
                const auto now = Clock::now();
                for (auto it = waiters_.begin(); it != waiters_.end();) {
                    if (now >= (*it)->deadline) {
                        (*it)->error = std::make_exception_ptr(DataRelayTimeout("No data received within timeout"));
                        (*it)->cv.notify_all();
                        it = waiters_.erase(it);
                    } else {
                        ++it;
                    }
                }
                if (require_waiter && waiters_.empty()) {
                    return false;
                }
                latest_ = data;
                sink = sink_;
                for (const auto &waiter : waiters_) {
                    waiter->data = data;
                    waiter->cv.notify_all();
                }
                waiters_.clear();
            }
            if (sink) {
                {
                    const std::lock_guard<std::mutex> lock(sink->mutex);
                    if (!sink->active) {
                        return true;
                    }
                    ++sink->running;
                }
                Invocation call{sink.get(), this, invocation_};
                invocation_ = &call;
                struct Finish {
                    Sink &sink;
                    Invocation *previous;
                    ~Finish() {
                        invocation_ = previous;
                        const std::lock_guard<std::mutex> lock(sink.mutex);
                        --sink.running;
                        sink.cv.notify_all();
                    }
                } finish{*sink, call.previous};
                sink->callback(data);
            }
            return true;
        }

        std::shared_ptr<Waiter> register_waiter(Deadline deadline, std::optional<size_t> epoch = {}) {
            auto waiter = std::make_shared<Waiter>();
            waiter->deadline = deadline;
            const std::lock_guard<std::mutex> lock(mutex_);
            if (cancelled_ || (epoch && *epoch != epoch_)) {
                throw ::linkerbot::hand::LifecycleError("cancelled");
            }
            if (Clock::now() >= deadline) {
                throw DataRelayTimeout("request deadline expired before send");
            }
            waiters_.push_back(waiter);
            return waiter;
        }
        void discard_waiter(const std::shared_ptr<Waiter> &waiter) {
            const std::lock_guard<std::mutex> lock(mutex_);
            waiters_.erase(std::remove(waiters_.begin(), waiters_.end(), waiter), waiters_.end());
        }
        T wait_until(const std::shared_ptr<Waiter> &waiter, Deadline deadline) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!waiter->cv.wait_until(lock, deadline, [&] { return waiter->data.has_value() || waiter->error; })) {
                waiters_.erase(std::remove(waiters_.begin(), waiters_.end(), waiter), waiters_.end());
                throw DataRelayTimeout("No data received within timeout");
            }
            if (waiter->error) {
                std::rethrow_exception(waiter->error);
            }
            return *waiter->data;
        }

        mutable std::mutex mutex_;
        std::condition_variable admission_cv_;
        bool cancelled_ = false;
        bool request_active_ = false;
        size_t epoch_ = 0;
        std::optional<T> latest_;
        std::vector<std::shared_ptr<Waiter>> waiters_;
        std::shared_ptr<Sink> sink_;
        std::vector<std::shared_ptr<Sink>> retired_sinks_;
    };
} // namespace linkerbot::internal

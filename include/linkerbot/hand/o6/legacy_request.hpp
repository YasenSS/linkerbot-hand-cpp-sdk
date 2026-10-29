#pragma once

#include <utility>

#include <cmath>
#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/relay.hpp>
#include <linkerbot/transport/session.hpp>

// Local protection for the classic O6/L20Lite wire protocols. There is no wire
// transaction number: an uncertain transaction stays quarantined until the
// manager is reconstructed. start()/stop() do not erase that evidence.
namespace linkerbot::hand::classic_detail {
    using Clock = std::chrono::steady_clock;
    struct SendContext {
        Clock::time_point deadline;
        std::function<bool()> cancelled;
        const void *owner;
        std::shared_ptr<transport::Session> *session;
        bool *submitted;
    };
    inline thread_local const SendContext *current_send = nullptr;
    class SendScope {
    public:
        explicit SendScope(const SendContext &context) : previous_(current_send) { current_send = &context; }
        ~SendScope() { current_send = previous_; }

    private:
        const SendContext *previous_;
    };
    inline bool valid_response(const can::Message &msg, uint32_t id) {
        return msg.id == id && !msg.is_extended && !msg.is_fd && !msg.is_brs && msg.dlc > 0 && msg.dlc <= 8;
    }
    inline void send_frames(can::CANDispatcher &dispatcher, const std::vector<can::Message> &frames,
                            std::chrono::microseconds gap = {}, std::chrono::microseconds initial = {}) {
        if (!current_send) {
            size_t sent = 0;
            bool uncertain = false;
            dispatcher.send_batch_until(frames, Clock::time_point::max(), gap, initial, {}, sent, uncertain);
            return;
        }
        // Retain the adapter on the manager: destroying a temporary Session after
        // a timeout would drain its writer and silently extend the API budget.
        auto &session = *current_send->session;
        if (!session) {
            session = transport::Session::borrow(dispatcher);
        }
        transport::CancellationSource cancellation;
        transport::SendOptions options;
        options.deadline = current_send->deadline;
        options.cancellation = cancellation.token();
        *current_send->submitted = true;
        const auto receipt = session->submit({frames, gap, initial}, options);
        struct CancelOnExit {
            transport::CancellationSource &source;
            ~CancelOnExit() { source.cancel(); }
        } cancel_on_exit{cancellation};
        for (;;) {
            if (current_send->cancelled()) {
                cancellation.cancel();
                throw LifecycleError("manager stopped during send");
            }
            transport::SendResult result;
            try {
                result =
                    receipt.wait_until(std::min(current_send->deadline, Clock::now() + std::chrono::milliseconds(1)));
            } catch (const can::TimeoutError &) {
                result = receipt.result();
            }
            if (result.error) {
                std::rethrow_exception(result.error);
            }
            if (result.outcome == transport::SendOutcome::dispatched) {
                if (Clock::now() < current_send->deadline) {
                    return;
                }
            }
            if (Clock::now() >= current_send->deadline || result.outcome == transport::SendOutcome::expired) {
                cancellation.cancel();
                throw internal::DataRelayTimeout("send deadline expired; response key quarantined");
            }
            if (result.outcome != transport::SendOutcome::pending) {
                throw LifecycleError("send cancelled");
            }
        }
    }
    inline std::chrono::milliseconds remaining(Clock::time_point deadline) {
        auto value = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (value.count() <= 0) {
            throw internal::DataRelayTimeout("operation deadline expired");
        }
        return value;
    }

    inline thread_local size_t callback_depth = 0;
    struct CallbackScope {
        CallbackScope() { ++callback_depth; }
        ~CallbackScope() { --callback_depth; }
    };

    // Count callers before admission, including callers which will be cancelled
    // while queued. A shared counter keeps guard teardown independent of the owner.
    class CallDrain {
        friend struct AdmissionTestAccess;
        struct State {
            std::mutex mutex;
            std::condition_variable cv;
            size_t calls = 0;
        };

    public:
        class Guard {
        public:
            explicit Guard(std::shared_ptr<State> state) : state_(std::move(state)) {
                const std::scoped_lock lock(state_->mutex);
                ++state_->calls;
                state_->cv.notify_all();
            }
            ~Guard() {
                const std::scoped_lock lock(state_->mutex);
                --state_->calls;
                state_->cv.notify_all();
            }
            Guard(const Guard &) = delete;
            Guard &operator=(const Guard &) = delete;

        private:
            std::shared_ptr<State> state_;
        };
        Guard enter() { return Guard(state_); }
        void drain() {
            // A synchronous sink may be inside the very call being cancelled.
            // Skipping here does not discard the counter: external stop still waits.
            if (callback_depth || can::CANDispatcher::is_any_receive_thread()) {
                return;
            }
            std::unique_lock lock(state_->mutex);
            state_->cv.wait(lock, [&] { return state_->calls == 0; });
        }

    private:
        std::shared_ptr<State> state_ = std::make_shared<State>();
    };

    // Serializes a composite public operation while counting admission against its
    // original deadline. Cancelling never revives a caller queued before restart.
    class OperationGate {
        friend struct AdmissionTestAccess;

    public:
        template <class Fn> decltype(std::declval<Fn &>()()) run_until(Clock::time_point deadline, Fn &&fn) {
            const auto call = calls_.enter();
            {
                std::unique_lock lock(mutex_);
                const auto epoch = epoch_;
                if (!cv_.wait_until(lock, deadline, [&] { return !busy_ || stopped_ || epoch != epoch_; })) {
                    throw internal::DataRelayTimeout("composite admission deadline expired");
                }
                if (stopped_ || epoch != epoch_) {
                    throw LifecycleError("manager stopped");
                }
                if (Clock::now() >= deadline) {
                    throw internal::DataRelayTimeout("composite deadline expired");
                }
                busy_ = true;
            }
            struct Release {
                OperationGate &gate;
                ~Release() {
                    const std::scoped_lock lock(gate.mutex_);
                    gate.busy_ = false;
                    gate.cv_.notify_all();
                }
            } release{*this};
            return std::forward<Fn>(fn)();
        }
        void cancel_pending() {
            const std::scoped_lock lock(mutex_);
            stopped_ = true;
            ++epoch_;
            cv_.notify_all();
        }
        void drain() { calls_.drain(); }
        void stop() {
            cancel_pending();
            drain();
        }
        void resume() {
            const std::scoped_lock lock(mutex_);
            stopped_ = false;
        }

    private:
        CallDrain calls_;
        std::mutex mutex_;
        std::condition_variable cv_;
        size_t epoch_ = 0;
        bool busy_ = false, stopped_ = false;
    };

    // Serialize lifecycle operations without holding a mutex across callback
    // cancellation. RX callers must reject before waiting for an external stopper.
    class LifecycleGate {
    public:
        class Guard {
        public:
            explicit Guard(LifecycleGate &gate) : gate_(gate) {
                std::unique_lock lock(gate_.mutex_);
                gate_.cv_.wait(lock, [&] { return !gate_.busy_; });
                gate_.busy_ = true;
            }
            ~Guard() {
                const std::scoped_lock lock(gate_.mutex_);
                gate_.busy_ = false;
                gate_.cv_.notify_all();
            }
            Guard(const Guard &) = delete;
            Guard &operator=(const Guard &) = delete;

        private:
            LifecycleGate &gate_;
        };
        Guard enter() {
            if (can::CANDispatcher::is_any_receive_thread() || callback_depth) {
                throw LifecycleError("manager lifecycle requires an external caller");
            }
            return Guard(*this);
        }
        Guard enter(can::CANDispatcher &dispatcher) {
            if (dispatcher.is_receive_thread() || callback_depth) {
                throw LifecycleError("manager lifecycle requires an external caller");
            }
            return Guard(*this);
        }

    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        bool busy_ = false;
    };

    template <typename T> class RequestRelay {
        friend struct AdmissionTestAccess;

    public:
        template <typename Send> T request(Send &&send, float seconds) {
            if (!std::isfinite(seconds) || seconds <= 0) {
                throw ValidationError("timeout must be positive and finite");
            }
            return request_until(std::forward<Send>(send), Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                                                              std::chrono::duration<float>(seconds)));
        }
        template <typename Send> T request_until(Send &&send, Clock::time_point deadline) {
            const auto call = calls_.enter();
            {
                std::unique_lock lock(mutex_);
                const auto epoch = epoch_;
                while (busy_ && !stopped_ && !poisoned_ && epoch == epoch_) {
                    expire_locked();
                    if (!busy_ || poisoned_) {
                        break;
                    }
                    if (Clock::now() >= deadline) {
                        throw internal::DataRelayTimeout("request admission deadline expired");
                    }
                    cv_.wait_until(lock, std::min(deadline, deadline_));
                }
                if (epoch != epoch_) {
                    throw LifecycleError("manager restarted during admission");
                }
                check_locked();
                if (Clock::now() >= deadline) {
                    throw internal::DataRelayTimeout("request deadline expired before send");
                }
                busy_ = true;
                asynchronous_ = false;
                delivered_ = publishing_ = false;
                ++generation_;
                deadline_ = deadline;
            }
            bool submitted = false;
            const SendContext context{deadline, [this] { return stopped(); }, this, &session_, &submitted};
            const SendScope scope(context);
            try {
                auto result = relay_.request_until(std::forward<Send>(send), deadline);
                if (Clock::now() >= deadline) {
                    throw internal::DataRelayTimeout("request deadline expired");
                }
                if (stopped()) {
                    throw LifecycleError("manager stopped");
                }
                finish(false);
                return result;
            } catch (...) {
                finish(submitted);
                throw;
            }
        }
        // Polling uses the same window as a blocking request. It cannot reset an
        // outstanding request, including a forced legacy sense call.
        template <typename Send>
        void send(Send &&send, Clock::time_point until = Clock::now() + std::chrono::milliseconds(200)) {
            const auto call = calls_.enter();
            if (current_send && current_send->owner == this) {
                std::forward<Send>(send)();
                return;
            }
            Clock::time_point deadline;
            {
                std::unique_lock lock(mutex_);
                expire_locked();
                check_locked();
                if (busy_) {
                    return;
                }
                busy_ = asynchronous_ = sending_ = true;
                delivered_ = publishing_ = false;
                ++generation_;
                deadline = deadline_ = until;
            }
            bool submitted = false;
            const SendContext context{deadline, [this] { return stopped(); }, this, &session_, &submitted};
            const SendScope scope(context);
            try {
                std::forward<Send>(send)();
                sent();
            } catch (...) {
                finish(submitted);
                throw;
            }
        }
        // Control writes keep their legacy synchronous send contract even when
        // replies are disabled. Ambiguous outstanding echoes quarantine reads,
        // but never silently drop a control command or add an ACK wait to it.
        template <class Send> void command(Send &&send) {
            const auto call = calls_.enter();
            {
                std::unique_lock lock(mutex_);
                const auto epoch = epoch_;
                cv_.wait(lock, [&] {
                    return !busy_ || (asynchronous_ && !sending_ && !publishing_) || stopped_ || epoch != epoch_;
                });
                if (stopped_ || epoch != epoch_) {
                    throw LifecycleError("manager stopped");
                }
                if (busy_ && !delivered_) {
                    poisoned_ = true;
                }
                busy_ = asynchronous_ = sending_ = true;
                delivered_ = publishing_ = false;
                ++generation_;
                deadline_ = Clock::now() + std::chrono::milliseconds(200);
            }
            try {
                std::forward<Send>(send)();
                sent();
            } catch (...) {
                finish(true);
                throw;
            }
        }
        bool receiving() {
            const std::scoped_lock lock(mutex_);
            expire_locked();
            return busy_ && !delivered_ && !stopped_ && !poisoned_;
        }
        void push(const T &value) {
            size_t generation;
            {
                const std::scoped_lock lock(mutex_);
                expire_locked();
                if (stopped_ || poisoned_ || (busy_ && delivered_)) {
                    return;
                }
                delivered_ = true;
                publishing_ = true;
                generation = generation_;
            }
            try {
                relay_.push(value);
            } catch (...) {
                published(generation);
                throw;
            }
            published(generation);
        }
        void conflict() {
            {
                const std::scoped_lock lock(mutex_);
                poisoned_ = true;
                cv_.notify_all();
            }
            relay_.fail(
                std::make_exception_ptr(ProtocolError("conflicting response; rebuild manager to leave quarantine")));
        }
        void cancel_pending() {
            {
                const std::scoped_lock lock(mutex_);
                stopped_ = true;
                ++epoch_;
                if (busy_ && !delivered_) {
                    poisoned_ = true;
                }
                cv_.notify_all();
            }
            relay_.cancel(std::make_exception_ptr(LifecycleError("manager stopped")));
        }
        void drain() { calls_.drain(); }
        void stop() {
            cancel_pending();
            drain();
        }
        ~RequestRelay() { stop(); }
        void resume() {
            const std::scoped_lock lock(mutex_);
            stopped_ = false;
            relay_.resume();
        }
        std::optional<T> snapshot() const { return relay_.snapshot(); }
        void set_sink(std::function<void(const T &)> sink) {
            if (!sink) {
                relay_.set_sink({});
                return;
            }
            relay_.set_sink([sink = std::move(sink)](const T &value) {
                const CallbackScope callback;
                sink(value);
            });
        }

    private:
        bool stopped() {
            const std::scoped_lock lock(mutex_);
            return stopped_;
        }
        void check_locked() {
            if (stopped_) {
                throw ClosedError("manager stopped");
            }
            if (poisoned_) {
                throw DeviceNotReadyError("response key quarantined; rebuild manager");
            }
        }
        void expire_locked() {
            if (busy_ && !delivered_ && Clock::now() >= deadline_) {
                poisoned_ = true;
            }
        }
        void published(size_t generation) {
            const std::scoped_lock lock(mutex_);
            if (generation != generation_) {
                return;
            }
            publishing_ = false;
            if (asynchronous_ && !sending_) {
                busy_ = asynchronous_ = false;
                cv_.notify_all();
            }
        }
        void sent() {
            const std::scoped_lock lock(mutex_);
            sending_ = false;
            if (delivered_ && !publishing_) {
                busy_ = asynchronous_ = false;
                cv_.notify_all();
            }
        }
        void finish(bool uncertain) {
            const std::scoped_lock lock(mutex_);
            if (uncertain) {
                poisoned_ = true;
            }
            busy_ = asynchronous_ = sending_ = false;
            cv_.notify_all();
        }
        CallDrain calls_;
        std::shared_ptr<transport::Session> session_;
        internal::DataRelay<T> relay_;
        std::mutex mutex_;
        std::condition_variable cv_;
        bool busy_ = false, asynchronous_ = false, delivered_ = false, sending_ = false, publishing_ = false;
        bool stopped_ = false, poisoned_ = false;
        size_t epoch_ = 0, generation_ = 0;
        Clock::time_point deadline_{};
    };
}

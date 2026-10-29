#pragma once
#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/runtime/call_drain.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/hand/runtime/event_binding.hpp>
#include <linkerbot/relay.hpp> // legacy timeout exception type only
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace linkerbot::hand::l6::detail {
    inline auto borrow_runtime(can::CANDispatcher &dispatcher, uint32_t id) {
        return runtime::DeviceRuntime::borrow(dispatcher, "l6/" + std::to_string(id), {{id, false}});
    }
    template <class T, class = void> struct HasTimestamp : std::false_type {};
    template <class T> struct HasTimestamp<T, std::void_t<decltype(std::declval<T>().timestamp)>> : std::true_type {};
    template <class T, class = void> struct HasSequence : std::false_type {};
    template <class T> struct HasSequence<T, std::void_t<decltype(std::declval<T>().sequence)>> : std::true_type {};
    // Value/API projection only. All routing, waiting, cancellation, correlation,
    // deadlines and snapshots belong to DeviceRuntime.
    template <class Data> class Channel {
    public:
        Channel(std::shared_ptr<runtime::DeviceRuntime> core, std::string domain)
            : core_(std::move(core)), domain_(std::move(domain)), events_(core_, domain_) {}
        ~Channel() {
            try {
                shutdown();
            } catch (...) {
            }
        }
        auto enter() { return calls_.enter(); }
        void drain_calls() { calls_.drain(); }
        auto core() const { return core_; }
        void start() {
            core_->assert_external_lifecycle();
            std::lock_guard<std::mutex> lifecycle(lifecycle_);
            std::lock_guard<std::mutex> lock(mutex_);
            if (cancel_.token().cancelled()) {
                cancel_ = runtime::CancellationSource{};
            }
            events_.open();
        }
        void observe(runtime::DeviceRuntime::Decoder decoder) {
            std::lock_guard<std::mutex> lock(mutex_);
            cancel_.token().throw_if_cancelled();
            if (!observation_) {
                observation_ = core_->observe(domain_, std::move(decoder));
            }
        }
        void cancel() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                cancel_.cancel();
            }
            core_->cancel_domain(domain_);
        }
        void stop() {
            // Callback cancellation never waits for itself or an external stopper.
            // A subsequent external stop still performs the full call/event drain.
            if (core_->is_event_thread() || can::CANDispatcher::is_any_receive_thread()) {
                cancel();
                events_.close();
                return;
            }
            core_->assert_external_lifecycle();
            shutdown();
        }
        runtime::CancellationToken token() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return cancel_.token();
        }
        static Data project(const runtime::Sample &sample) {
            auto data = std::any_cast<Data>(sample.value);
            if constexpr (HasTimestamp<Data>::value) {
                data.timestamp = sample.received_at;
            }
            if constexpr (HasSequence<Data>::value) {
                data.sequence = sample.sequence;
                data.generation = sample.transaction_generation;
                data.latency =
                    std::chrono::duration_cast<std::chrono::microseconds>(sample.received_at - sample.started_at);
            }
            return data;
        }
        Data get(runtime::TransactionPlan plan, runtime::Deadline deadline) {
            const auto call = enter();
            try {
                return project(core_->execute(std::move(plan), deadline, token()));
            } catch (const ::linkerbot::hand::TimeoutError &error) {
                throw internal::DataRelayTimeout(error.what(), error.context(), std::current_exception());
            }
        }
        void request(runtime::TransactionPlan plan, runtime::Deadline deadline) {
            const auto call = enter();
            if (plan.command_family.empty() && plan.optional_response_window) {
                plan.command_family = domain_;
            }
            (void)core_->submit(std::move(plan), deadline, token()).wait_sent_until(deadline);
        }
        void command(runtime::TransactionPlan plan) {
            const auto call = enter();
            if (plan.domain.empty()) {
                plan.domain = domain_;
            }
            if (plan.command_family.empty()) {
                plan.command_family = domain_;
            }
            core_->command(std::move(plan), runtime::Deadline::max(), token());
        }
        std::optional<Data> snapshot() const {
            auto sample = core_->snapshot(domain_);
            if (!sample || !sample->valid) {
                return std::nullopt;
            }
            return project(*sample);
        }
        void set_sink(std::function<void(const Data &)> sink) {
            if (sink) {
                events_.replace([sink = std::move(sink)](const auto &sample) { sink(project(sample)); });
            } else {
                events_.replace({});
            }
        }
        size_t pending_count() const { return core_->pending_count(domain_); }

    private:
        void shutdown() {
            std::lock_guard<std::mutex> lifecycle(lifecycle_);
            cancel();
            calls_.drain();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (observation_) {
                    core_->unobserve(*observation_);
                    observation_.reset();
                }
            }
            events_.close();
        }
        runtime::CallDrain calls_;
        std::mutex lifecycle_;
        std::shared_ptr<runtime::DeviceRuntime> core_;
        std::string domain_;
        mutable std::mutex mutex_;
        runtime::CancellationSource cancel_;
        std::optional<runtime::DeviceRuntime::HandlerId> observation_;
        runtime::EventBinding events_;
    };
} // namespace linkerbot::hand::l6::detail

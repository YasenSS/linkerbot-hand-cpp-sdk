#include <linkerbot/hand/errors.hpp>
#pragma once
#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/runtime/call_drain.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/hand/runtime/event_binding.hpp>
#include <linkerbot/relay.hpp>
#include <type_traits>

namespace linkerbot::hand::runtime {
    template <class T, class = void> struct HasTimestamp : std::false_type {};
    template <class T> struct HasTimestamp<T, std::void_t<decltype(std::declval<T>().timestamp)>> : std::true_type {};

    template <class Data> class Channel {
        friend struct ChannelTestAccess;

    public:
        Channel(std::shared_ptr<DeviceRuntime> core, std::string domain, bool preserve = false)
            : core_(std::move(core)), domain_(std::move(domain)), events_(core_, domain_), preserve_(preserve) {
            if (!core_) {
                throw ValidationError("channel requires a runtime");
            }
        }
        ~Channel() {
            try {
                shutdown();
            } catch (...) {
            }
        }
        auto enter() { return calls_.enter(); }
        auto core() const { return core_; }
        void start(DeviceRuntime::Decoder decoder = {}) {
            assert_external();
            std::lock_guard<std::mutex> lifecycle(lifecycle_);
            std::lock_guard<std::mutex> lock(mutex_);
            if (cancel_.token().cancelled()) {
                cancel_ = CancellationSource{};
            }
            events_.open();
            if (decoder && !observation_) {
                observation_ = core_->observe(domain_, std::move(decoder));
            }
        }
        void cancel() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                cancel_.cancel();
            }
            core_->cancel_domain(domain_, preserve_);
        }
        void stop(bool allow_callback_cancel = false) {
            if (allow_callback_cancel && (core_->is_event_thread() || can::CANDispatcher::is_any_receive_thread())) {
                // Legacy callbacks may cancel themselves; an external stop still
                // owns the drain/barrier. Never acquire lifecycle_ from the callback.
                cancel();
                events_.close();
                return;
            }
            assert_external();
            shutdown();
        }
        CancellationToken token() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return cancel_.token();
        }
        static Data project(const Sample &sample) {
            auto data = std::any_cast<Data>(sample.value);
            if constexpr (HasTimestamp<Data>::value) {
                data.timestamp = sample.received_at;
            }
            return data;
        }
        Data get(TransactionPlan plan, Deadline deadline) {
            auto call = enter();
            try {
                return project(core_->execute(std::move(plan), deadline, token()));
            } catch (const ::linkerbot::hand::TimeoutError &error) {
                throw internal::DataRelayTimeout(error.what(), error.context(), std::current_exception());
            }
        }
        void request(TransactionPlan plan, Deadline deadline) {
            auto call = enter();
            if (plan.command_family.empty() && plan.optional_response_window) {
                plan.command_family = domain_;
            }
            (void)core_->submit(std::move(plan), deadline, token()).wait_sent_until(deadline);
        }
        void command(TransactionPlan plan) {
            auto call = enter();
            if (plan.domain.empty()) {
                plan.domain = domain_;
            }
            if (plan.command_family.empty()) {
                plan.command_family = domain_;
            }
            core_->command(std::move(plan), Deadline::max(), token());
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
                events_.replace([sink = std::move(sink)](const auto &s) { sink(project(s)); });
            } else {
                events_.replace({});
            }
        }

    private:
        void assert_external() const { core_->assert_external_lifecycle(); }
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
        std::shared_ptr<DeviceRuntime> core_;
        std::string domain_;
        mutable std::mutex mutex_;
        std::mutex lifecycle_;
        CancellationSource cancel_;
        CallDrain calls_;
        std::optional<DeviceRuntime::HandlerId> observation_;
        EventBinding events_;
        bool preserve_;
    };
} // namespace linkerbot::hand::runtime

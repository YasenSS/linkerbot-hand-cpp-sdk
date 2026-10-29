#pragma once
#include <functional>
#include <linkerbot/hand/runtime/event_binding.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace linkerbot::hand::runtime {
    // API-domain projection over the common snapshot/event store. It deliberately
    // has no waiter, RX subscription, parser or independent sample cache.
    template <class T> class Cache {
    public:
        Cache(std::shared_ptr<DeviceRuntime> runtime, std::string domain)
            : runtime_(std::move(runtime)), domain_(std::move(domain)), events_(runtime_, domain_) {}
        Cache(const Cache &) = delete;
        Cache &operator=(const Cache &) = delete;
        std::optional<T> snapshot() const { return runtime_->snapshot_as<T>(domain_); }
        void push(const T &value) { runtime_->publish(domain_, value); }
        void set_sink(std::function<void(const T &)> sink) {
            if (sink) {
                events_.replace(
                    [sink = std::move(sink)](const Sample &sample) { sink(std::any_cast<const T &>(sample.value)); });
            } else {
                events_.replace({});
            }
        }

    private:
        std::shared_ptr<DeviceRuntime> runtime_;
        std::string domain_;
        EventBinding events_;
    };
} // namespace linkerbot::hand::runtime

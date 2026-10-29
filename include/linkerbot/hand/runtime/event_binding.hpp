#pragma once

#include <linkerbot/hand/errors.hpp>

#include <algorithm>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace linkerbot::hand::runtime {
    // Owns subscriptions for a facade's replaceable event sink. Waiting belongs to
    // DeviceRuntime; no facade mutex is held across its callback cancellation barrier.
    class EventBinding {
    public:
        EventBinding(std::shared_ptr<DeviceRuntime> core, std::string domain)
            : core_(std::move(core)), domain_(std::move(domain)) {}
        ~EventBinding() {
            try {
                close();
            } catch (...) {
            }
        }
        EventBinding(const EventBinding &) = delete;
        EventBinding &operator=(const EventBinding &) = delete;

        void open() {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = false;
        }
        void replace(DeviceRuntime::EventHandler handler) {
            std::vector<DeviceRuntime::HandlerId> old;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (closed_ && handler) {
                    throw ClosedError("event binding is closed");
                }
                old = ids_;
                if (handler) {
                    ids_.reserve(ids_.size() + 1);
                    ids_.push_back(core_->subscribe(domain_, std::move(handler)));
                }
            }
            cancel(old);
        }
        void close() {
            std::vector<DeviceRuntime::HandlerId> old;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                old = ids_;
                closed_ = true;
            }
            cancel(old);
        }

    private:
        void cancel(const std::vector<DeviceRuntime::HandlerId> &ids) {
            for (const auto id : ids) {
                if (core_->unsubscribe(id)) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ids_.erase(std::remove(ids_.begin(), ids_.end(), id), ids_.end());
                }
            }
        }
        std::shared_ptr<DeviceRuntime> core_;
        std::string domain_;
        std::mutex mutex_;
        bool closed_ = false;
        std::vector<DeviceRuntime::HandlerId> ids_;
    };
} // namespace linkerbot::hand::runtime

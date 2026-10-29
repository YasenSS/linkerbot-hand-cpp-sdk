#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/o6/current.hpp>
#include <linkerbot/hand/o6/detail/channel.hpp>
#include <linkerbot/hand/o6/protocol_backend.hpp>

namespace linkerbot::hand::o6 {
    class CurrentManager {
        friend class O6;
        friend struct ManagerTestAccess;

    public:
        CurrentManager(uint32_t id, can::CANDispatcher &dispatcher);
        CurrentManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~CurrentManager();
        CurrentManager(const CurrentManager &) = delete;
        CurrentManager &operator=(const CurrentManager &) = delete;
        void start();
        void stop();
        [[nodiscard]] CurrentData get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
        [[nodiscard]] std::optional<CurrentData> get_snapshot() const;

    private:
        void cancel_pending() { channel_.cancel(); }
        void send_sense_request();
        void set_event_sink(std::function<void(const CurrentData &)> sink);
        uint32_t id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        detail::Channel<CurrentData> channel_;
    };

    class CurrentLimitManager {
        friend class O6;

    public:
        CurrentLimitManager(uint32_t id, can::CANDispatcher &dispatcher);
        CurrentLimitManager(uint32_t id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~CurrentLimitManager();
        CurrentLimitManager(const CurrentLimitManager &) = delete;
        CurrentLimitManager &operator=(const CurrentLimitManager &) = delete;
        void start();
        void stop();
        void set_current_limits(const std::array<uint8_t, kCurrentJointCount> &values);
        [[nodiscard]] std::optional<CurrentLimitData> get_snapshot() const;

    private:
        void cancel_pending() { channel_.cancel(); }
        void set_event_sink(std::function<void(const CurrentLimitData &)> sink);
        uint32_t id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        detail::Channel<CurrentLimitData> channel_;
    };
} // namespace linkerbot::hand::o6

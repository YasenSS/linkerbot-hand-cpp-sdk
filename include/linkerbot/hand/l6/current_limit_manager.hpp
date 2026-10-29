#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l6/current_limit.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>

namespace linkerbot::hand::l6 {
    class CurrentLimitManager {
        friend class L6;

    public:
        CurrentLimitManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        CurrentLimitManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~CurrentLimitManager();

        CurrentLimitManager(const CurrentLimitManager &) = delete;
        CurrentLimitManager &operator=(const CurrentLimitManager &) = delete;

        void start();
        void stop();
        void set_current_limits(const CurrentLimit &limits);
        void set_current_limits(const std::array<uint8_t, CurrentLimit::kJointCount> &values);
        [[nodiscard]] CurrentLimitData get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
        [[nodiscard]] std::optional<CurrentLimitData> get_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const CurrentLimitData &)> sink);
        [[nodiscard]] size_t pending_count() const;
        void cancel_waiters();
        void resume_transactions();

        uint32_t arbitration_id_;
        detail::Channel<CurrentLimitData> channel_;
    };
} // namespace linkerbot::hand::l6

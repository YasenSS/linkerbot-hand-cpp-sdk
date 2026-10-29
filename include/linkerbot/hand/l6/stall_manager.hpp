#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>
#include <linkerbot/hand/l6/stall.hpp>

namespace linkerbot::hand::l6 {

    class StallManager {
        friend class L6;

    public:
        StallManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        StallManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~StallManager();

        StallManager(const StallManager &) = delete;
        StallManager &operator=(const StallManager &) = delete;
        StallManager(StallManager &&) = delete;
        StallManager &operator=(StallManager &&) = delete;

        void start();
        void stop();

        void set_times(const StallTime &times);
        void set_times(const std::array<float, StallTime::kJointCount> &values);
        void set_thresholds(const StallLimit &limits);
        void set_thresholds(const std::array<float, StallLimit::kJointCount> &values);
        void set_torques(const StallLimit &limits);
        void set_torques(const std::array<float, StallLimit::kJointCount> &values);

        [[nodiscard]] StallTimeData get_times_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                           100});
        [[nodiscard]] StallLimitData
        get_thresholds_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        [[nodiscard]] StallLimitData
        get_torques_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<StallTimeData> get_time_snapshot() const;
        [[nodiscard]] std::optional<StallLimitData> get_threshold_snapshot() const;
        [[nodiscard]] std::optional<StallLimitData> get_torque_snapshot() const;

    private:
        static constexpr uint8_t kTimeCmd = 0xC5;
        static constexpr uint8_t kThresholdCmd = 0xC6;
        static constexpr uint8_t kTorqueCmd = 0xC7;

        void send_joint_frame(uint8_t cmd, const std::array<uint8_t, StallTime::kJointCount> &raw);
        void send_sense_request(uint8_t cmd);

        uint32_t arbitration_id_;
        detail::Channel<StallTimeData> time_;
        detail::Channel<StallLimitData> threshold_, torque_;
    };

} // namespace linkerbot::hand::l6

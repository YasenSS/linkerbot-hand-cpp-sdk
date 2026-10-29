#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/o6/detail/channel.hpp>
#include <linkerbot/hand/o6/protocol_backend.hpp>
#include <linkerbot/hand/o6/stall.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/relay.hpp>

namespace linkerbot::hand::o6 {

    class StallManager {
        friend class O6;
        friend struct ManagerTestAccess;

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
        std::mutex lifecycle_mutex_;
        void cancel_pending() {
            time_channel_.cancel();
            threshold_channel_.cancel();
            torque_channel_.cancel();
        }
        void send_time_sense_request();
        void send_threshold_sense_request();
        void send_torque_sense_request();

        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        detail::Channel<StallTimeData> time_channel_;
        detail::Channel<StallLimitData> threshold_channel_;
        detail::Channel<StallLimitData> torque_channel_;
    };

} // namespace linkerbot::hand::o6

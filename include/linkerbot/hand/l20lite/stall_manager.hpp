#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l20lite/detail/channel.hpp>
#include <linkerbot/hand/l20lite/protocol_backend.hpp>
#include <linkerbot/hand/l20lite/stall.hpp>
#include <linkerbot/relay.hpp>

namespace linkerbot::hand::l20lite {

    class StallManager {
        friend class L20lite;

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
        void set_times(const StallTime &value, std::chrono::milliseconds timeout_ms);
        void set_times(const std::array<float, StallTime::kJointCount> &values, std::chrono::milliseconds timeout_ms);
        void set_thresholds(const StallLimit &limits);
        void set_thresholds(const std::array<float, StallLimit::kJointCount> &values);
        void set_thresholds(const StallLimit &value, std::chrono::milliseconds timeout_ms);
        void set_thresholds(const std::array<float, StallLimit::kJointCount> &values,
                            std::chrono::milliseconds timeout_ms);
        void set_torques(const StallLimit &limits);
        void set_torques(const std::array<float, StallLimit::kJointCount> &values);
        void set_torques(const StallLimit &value, std::chrono::milliseconds timeout_ms);
        void set_torques(const std::array<float, StallLimit::kJointCount> &values,
                         std::chrono::milliseconds timeout_ms);

        // Setters wait for both firmware bank responses (100 ms by default).
        // Getters return the latest complete write-response cache without I/O.
        // Their timeout argument is retained for compatibility and validated.
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
        void cancel_pending();
        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        std::mutex lifecycle_mutex_;
        detail::Channel<StallTimeData> time_channel_;
        detail::Channel<StallLimitData> threshold_channel_, torque_channel_;
    };
}

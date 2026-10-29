#pragma once

#include <cstdint>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>
#include <linkerbot/hand/l6/raw_control_manager.hpp>
#include <linkerbot/hand/l6/speed.hpp>

namespace linkerbot::hand::l6 {

    class SpeedManager {
        friend class L6;

    public:
        SpeedManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        SpeedManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~SpeedManager() = default;

        SpeedManager(const SpeedManager &) = delete;
        SpeedManager &operator=(const SpeedManager &) = delete;
        SpeedManager(SpeedManager &&) = delete;
        SpeedManager &operator=(SpeedManager &&) = delete;

        void set_speeds(const Speed &speeds);
        void set_speeds(const std::array<float, Speed::kJointCount> &values);
        // Exact protocol counts; waits for the command's device response.
        void set_raw_speeds(const std::array<uint8_t, Speed::kJointCount> &values,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
        [[nodiscard]] std::optional<RawControlData> get_snapshot() const;

    private:
        static constexpr uint8_t kControlCmd = 0x05;

        uint32_t arbitration_id_;
        void start();
        detail::Channel<RawControlData> channel_;
    };

} // namespace linkerbot::hand::l6

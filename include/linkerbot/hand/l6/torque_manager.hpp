#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>
#include <linkerbot/hand/l6/frame.hpp>
#include <linkerbot/hand/l6/torque.hpp>

namespace linkerbot::hand::l6 {

    class TorqueManager {
        friend class L6;

    public:
        TorqueManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        TorqueManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~TorqueManager();

        TorqueManager(const TorqueManager &) = delete;
        TorqueManager &operator=(const TorqueManager &) = delete;
        TorqueManager(TorqueManager &&) = delete;
        TorqueManager &operator=(TorqueManager &&) = delete;

        void start();
        void stop();

        void set_torques(const Torque &torques);
        void set_torques(const std::array<uint8_t, Torque::kJointCount> &values);

        [[nodiscard]] TorqueData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<TorqueData> get_snapshot() const;

    private:
        static constexpr uint8_t kControlCmd = kTorqueOpcode;
        static constexpr uint8_t kSenseCmd = kTorqueOpcode;

        void send_sense_request();
        void set_event_sink(std::function<void(const TorqueData &)> sink);
        [[nodiscard]] size_t pending_count() const;
        void cancel_waiters();
        void resume_transactions();

        uint32_t arbitration_id_;
        detail::Channel<TorqueData> channel_;
    };

} // namespace linkerbot::hand::l6

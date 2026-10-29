#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/o6/acceleration.hpp>
#include <linkerbot/hand/o6/detail/channel.hpp>
#include <linkerbot/hand/o6/protocol_backend.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/relay.hpp>

namespace linkerbot::hand::o6 {

    class AccelerationManager {
        friend class O6;
        friend struct ManagerTestAccess;

    public:
        AccelerationManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        AccelerationManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~AccelerationManager();

        AccelerationManager(const AccelerationManager &) = delete;
        AccelerationManager &operator=(const AccelerationManager &) = delete;
        AccelerationManager(AccelerationManager &&) = delete;
        AccelerationManager &operator=(AccelerationManager &&) = delete;

        void start();
        void stop();

        void set_accelerations(const Acceleration &accelerations);
        void set_accelerations(const std::array<float, Acceleration::kJointCount> &values);

        [[nodiscard]] AccelerationData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                        100});

        [[nodiscard]] std::optional<AccelerationData> get_snapshot() const;

    private:
        void cancel_pending() { channel_.cancel(); }
        void send_sense_request();
        void set_event_sink(std::function<void(const AccelerationData &)> sink);

        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        detail::Channel<AccelerationData> channel_;
    };

} // namespace linkerbot::hand::o6

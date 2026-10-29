#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/o6/detail/channel.hpp>
#include <linkerbot/hand/o6/protocol_backend.hpp>
#include <linkerbot/hand/o6/temperature.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/relay.hpp>

namespace linkerbot::hand::o6 {

    class TemperatureManager {
        friend class O6;
        friend struct ManagerTestAccess;

    public:
        TemperatureManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        TemperatureManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~TemperatureManager();

        TemperatureManager(const TemperatureManager &) = delete;
        TemperatureManager &operator=(const TemperatureManager &) = delete;
        TemperatureManager(TemperatureManager &&) = delete;
        TemperatureManager &operator=(TemperatureManager &&) = delete;

        void start();
        void stop();

        [[nodiscard]] TemperatureData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                       100});

        [[nodiscard]] std::optional<TemperatureData> get_snapshot() const;

    private:
        void cancel_pending() { channel_.cancel(); }
        void send_sense_request();
        void set_event_sink(std::function<void(const TemperatureData &)> sink);

        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        detail::Channel<TemperatureData> channel_;
    };

} // namespace linkerbot::hand::o6

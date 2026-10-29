#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l20/detail/channel.hpp>
#include <linkerbot/hand/l20/protocol_backend.hpp>
#include <linkerbot/hand/l20/temperature.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>

namespace linkerbot::hand::l20 {
    class TemperatureManager {
        friend class L20;

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

} // namespace linkerbot::hand::l20

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>
#include <linkerbot/hand/l6/temperature.hpp>

namespace linkerbot::hand::l6 {

    class TemperatureManager {
        friend class L6;

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
        static constexpr uint8_t kSenseCmd = 0x33;

        void send_sense_request();
        void set_event_sink(std::function<void(const TemperatureData &)> sink);
        [[nodiscard]] size_t pending_count() const;
        void cancel_waiters();
        void resume_transactions();

        uint32_t arbitration_id_;
        detail::Channel<TemperatureData> channel_;
    };

} // namespace linkerbot::hand::l6

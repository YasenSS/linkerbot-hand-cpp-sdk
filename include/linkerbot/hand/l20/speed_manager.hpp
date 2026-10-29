#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l20/detail/channel.hpp>
#include <linkerbot/hand/l20/protocol_backend.hpp>
#include <linkerbot/hand/l20/speed.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>

namespace linkerbot::hand::l20 {
    class SpeedManager {
        friend class L20;

    public:
        SpeedManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        SpeedManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~SpeedManager();

        SpeedManager(const SpeedManager &) = delete;
        SpeedManager &operator=(const SpeedManager &) = delete;
        SpeedManager(SpeedManager &&) = delete;
        SpeedManager &operator=(SpeedManager &&) = delete;

        void start();
        void stop();

        void set_speeds(const Speed &speeds);
        void set_speeds(const std::array<float, Speed::kJointCount> &values);

        [[nodiscard]] SpeedData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<SpeedData> get_snapshot() const;

    private:
        void cancel_pending() { channel_.cancel(); }
        void send_sense_request();
        void set_event_sink(std::function<void(const SpeedData &)> sink);

        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        detail::Channel<SpeedData> channel_;
    };

} // namespace linkerbot::hand::l20

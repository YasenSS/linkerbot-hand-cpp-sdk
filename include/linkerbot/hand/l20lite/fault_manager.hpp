#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l20lite/detail/channel.hpp>
#include <linkerbot/hand/l20lite/fault.hpp>
#include <linkerbot/hand/l20lite/protocol_backend.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>

namespace linkerbot::hand::l20lite {
    class FaultManager {
        friend class L20lite;

    public:
        FaultManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        FaultManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~FaultManager();

        FaultManager(const FaultManager &) = delete;
        FaultManager &operator=(const FaultManager &) = delete;
        FaultManager(FaultManager &&) = delete;
        FaultManager &operator=(FaultManager &&) = delete;

        void start();
        void stop();

        [[nodiscard]] FaultData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<FaultData> get_snapshot() const;

    private:
        void cancel_pending() { channel_.cancel(); }
        void send_fault_request();
        void set_event_sink(std::function<void(const FaultData &)> sink);

        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        detail::Channel<FaultData> channel_;
    };

} // namespace linkerbot::hand::l20lite

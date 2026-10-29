#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>
#include <linkerbot/hand/l6/fault.hpp>

namespace linkerbot::hand::l6 {

    class FaultManager {
        friend class L6;

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

        void clear_faults();

        [[nodiscard]] FaultData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<FaultData> get_snapshot() const;

    private:
        static constexpr uint8_t kClearFaultCmd = 0x83;
        static constexpr uint8_t kReadFaultCmd = 0x35;

        void send_fault_request();
        void set_event_sink(std::function<void(const FaultData &)> sink);
        [[nodiscard]] size_t pending_count() const;
        void cancel_waiters();
        void resume_transactions();

        uint32_t arbitration_id_;
        detail::Channel<FaultData> channel_;
    };

} // namespace linkerbot::hand::l6

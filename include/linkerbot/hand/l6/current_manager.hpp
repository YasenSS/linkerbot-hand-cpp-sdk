#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l6/current.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>

namespace linkerbot::hand::l6 {

    class CurrentManager {
        friend class L6;

    public:
        CurrentManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        CurrentManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~CurrentManager();

        CurrentManager(const CurrentManager &) = delete;
        CurrentManager &operator=(const CurrentManager &) = delete;
        CurrentManager(CurrentManager &&) = delete;
        CurrentManager &operator=(CurrentManager &&) = delete;

        void start();
        void stop();

        [[nodiscard]] CurrentData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] std::optional<CurrentData> get_snapshot() const;

    private:
        static constexpr uint8_t kSenseCmd = 0x36;

        void send_sense_request();
        void set_event_sink(std::function<void(const CurrentData &)> sink);
        [[nodiscard]] size_t pending_count() const;
        void cancel_waiters();
        void resume_transactions();

        uint32_t arbitration_id_;
        detail::Channel<CurrentData> channel_;
    };

} // namespace linkerbot::hand::l6

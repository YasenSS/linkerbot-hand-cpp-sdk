#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l20lite/detail/channel.hpp>
#include <linkerbot/hand/l20lite/protocol_backend.hpp>
#include <linkerbot/hand/l20lite/tip_force.hpp>
#include <linkerbot/relay.hpp>

namespace linkerbot::hand::l20lite {

    class TipForceManager {
        friend class L20lite;

    public:
        TipForceManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        TipForceManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~TipForceManager();

        TipForceManager(const TipForceManager &) = delete;
        TipForceManager &operator=(const TipForceManager &) = delete;
        TipForceManager(TipForceManager &&) = delete;
        TipForceManager &operator=(TipForceManager &&) = delete;

        void start();
        void stop();

        [[nodiscard]] TipForceData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        [[nodiscard]] TipFinger get_finger_blocking(const std::string &name,
                                                    std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                        100});
        [[nodiscard]] std::optional<TipForceData> get_snapshot() const;

    private:
        void cancel_pending();
        void send_sense_request();
        void set_event_sink(std::function<void(const TipForceData &)> sink);
        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        std::mutex lifecycle_mutex_;
        std::array<std::unique_ptr<detail::Channel<TipFinger>>, 5> fingers_;
        detail::Channel<TipForceData> channel_;
    };
}

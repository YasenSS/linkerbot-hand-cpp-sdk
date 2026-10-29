#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l20lite/config.hpp>
#include <linkerbot/hand/l20lite/detail/channel.hpp>
#include <linkerbot/hand/l20lite/protocol_backend.hpp>
#include <linkerbot/relay.hpp>

namespace linkerbot::hand::l20lite {

    class ConfigManager {
        friend class L20lite;

    public:
        ConfigManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        ConfigManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~ConfigManager();

        ConfigManager(const ConfigManager &) = delete;
        ConfigManager &operator=(const ConfigManager &) = delete;
        ConfigManager(ConfigManager &&) = delete;
        ConfigManager &operator=(ConfigManager &&) = delete;

        void start();
        void stop();

        CanIds set_id(uint8_t id, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        CanIds set_device_id(uint8_t id, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        CanIds set_transmit_id(uint8_t id, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        void set_reply_enabled(bool enabled);
        void restore_factory();
        void save();

    private:
        void cancel_pending() { channel_.cancel(); }
        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        detail::Channel<CanIds> channel_;
    };
}

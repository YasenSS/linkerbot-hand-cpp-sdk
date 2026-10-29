#pragma once

#include <cstdint>
#include <memory>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/o6/config.hpp>
#include <linkerbot/hand/o6/protocol_backend.hpp>
#include <linkerbot/hand/runtime/channel.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>

namespace linkerbot::hand::o6 {

    class ConfigManager {
        friend class O6;

    public:
        ConfigManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher);
        ConfigManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime);
        ~ConfigManager() = default;

        ConfigManager(const ConfigManager &) = delete;
        ConfigManager &operator=(const ConfigManager &) = delete;
        ConfigManager(ConfigManager &&) = delete;
        ConfigManager &operator=(ConfigManager &&) = delete;

        // 0xC3: BYTE1 0x01 both IDs, 0x02 device ID, 0x03 transmit ID.
        void set_id(uint8_t id);
        void set_device_id(uint8_t id);
        void set_transmit_id(uint8_t id);

        // 0xCE: restore factory settings (8-byte fill).
        void restore_factory();

        // 0xCF: persist parameters (8-byte fill).
        void save();

    private:
        friend class O6;
        void cancel_pending() { channel_.cancel(); }
        void stop() { channel_.stop(); }
        uint32_t arbitration_id_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        protocol::Backend protocol_;
        runtime::Channel<int> channel_;
    };

} // namespace linkerbot::hand::o6

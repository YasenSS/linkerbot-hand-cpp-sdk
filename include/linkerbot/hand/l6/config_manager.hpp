#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/hand/l6/config.hpp>
#include <linkerbot/hand/l6/detail/channel.hpp>

namespace linkerbot::hand::l6 {

    class ConfigManager {
        friend class L6;
        friend struct L6RuntimeTestAccess;

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

        // 0xC3: BYTE1=0x01 sets TX ID, BYTE1=0x02 sets RX ID. Reply is TX then RX.
        CanIds set_id(uint8_t id, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        CanIds set_transmit_id(uint8_t id, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        CanIds set_receive_id(uint8_t id, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        // 0xCC: last byte 0x01 keeps replies on, 0xCC turns them off.
        void set_reply_enabled(bool enabled);
        [[nodiscard]] std::optional<bool> reply_enabled() const;

        // 0xCE: restore factory settings (C5/C6/C7 and ID).
        void restore_factory();

        // 0xCF: persist C5/C6/C7 and ID (new protocol: 7-byte frame).
        void save();

    private:
        static constexpr uint8_t kSetIdCmd = 0xC3;
        static constexpr uint8_t kSetTxIdSubcmd = 0x01;
        static constexpr uint8_t kSetRxIdSubcmd = 0x02;
        static constexpr uint8_t kReplyCmd = 0xCC;
        static constexpr uint8_t kFactoryCmd = 0xCE;
        static constexpr uint8_t kSaveCmd = 0xCF;

        CanIds set_id_with_subcmd(uint8_t subcmd, uint8_t id, std::chrono::milliseconds timeout_ms);
        void send_filled(uint8_t cmd, uint8_t last_byte, uint8_t dlc);
        void set_reply_change_listener(std::function<void(bool)> listener);

        uint32_t arbitration_id_;
        detail::Channel<CanIds> ids_;
        mutable std::mutex reply_mutex_;
        std::optional<bool> reply_enabled_;
        std::function<void(bool)> reply_listener_;
    };

} // namespace linkerbot::hand::l6

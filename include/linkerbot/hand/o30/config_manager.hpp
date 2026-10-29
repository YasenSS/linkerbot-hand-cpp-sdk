#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/config.hpp>

namespace linkerbot::hand::o30 {

    class ConfigManager {
        friend class O30;

    public:
        explicit ConfigManager(Client &client);

        ConfigManager(const ConfigManager &) = delete;
        ConfigManager &operator=(const ConfigManager &) = delete;
        ConfigManager(ConfigManager &&) = delete;
        ConfigManager &operator=(ConfigManager &&) = delete;

        [[nodiscard]] DeviceConfig get(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

        // On successful standard-ID write the client automatically follows
        // the new ID. A missing ACK leaves the active device ID uncertain;
        // the call throws and retains the previous client ID.
        void set_standard_id(uint16_t id, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_extended_id(uint32_t id, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        void set_control_mode(uint8_t mode, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_led(uint8_t value, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_buzzer(uint8_t value, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        // SI 0x1C / 0x1D / 0x1E. confirm must be true.
        void restore_config(bool confirm, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});
        void restore_all(bool confirm, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});
        void save_to_flash(bool confirm, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

        // Raw field access: firmware may reject unverified blocks. Restore,
        // save and partial standard-ID writes are rejected locally.
        void write_field(uint8_t si, const std::vector<uint8_t> &payload,
                         std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

    private:
        void write_confirm(uint8_t si, bool confirm, std::chrono::milliseconds timeout_ms);

        Client *client_;
    };

} // namespace linkerbot::hand::o30

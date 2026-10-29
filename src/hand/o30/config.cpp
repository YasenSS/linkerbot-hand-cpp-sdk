#include "linkerbot/hand/o30/config.hpp"
#include <linkerbot/hand/errors.hpp>

#include <stdexcept>
#include <vector>

#include <linkerbot/hand/o30/config_manager.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    namespace {
        constexpr uint8_t kSiStandardId = 0x00;
        constexpr uint8_t kSiExtendedId = 0x02;
        constexpr uint8_t kSiCanType = 0x06;
        constexpr uint8_t kSiBrs = 0x07;
        constexpr uint8_t kSiControlMode = 0x18;
        constexpr uint8_t kSiLed = 0x19;
        constexpr uint8_t kSiBuzzer = 0x1A;
        constexpr uint8_t kSiRestoreConfig = 0x1C;
        constexpr uint8_t kSiRestoreAll = 0x1D;
        constexpr uint8_t kSiSaveFlash = 0x1E;
    } // namespace

    uint16_t DeviceConfig::standard_id() const { return protocol::read_u16_le(raw.data() + kSiStandardId); }

    uint32_t DeviceConfig::extended_id() const { return protocol::read_u32_le(raw.data() + kSiExtendedId); }

    uint8_t DeviceConfig::can_type() const { return raw[kSiCanType]; }

    uint8_t DeviceConfig::brs() const { return raw[kSiBrs]; }

    uint8_t DeviceConfig::control_mode() const { return raw[kSiControlMode]; }

    uint8_t DeviceConfig::led() const { return raw[kSiLed]; }

    uint8_t DeviceConfig::buzzer() const { return raw[kSiBuzzer]; }

    ConfigManager::ConfigManager(Client &client) : client_(&client) {}

    DeviceConfig ConfigManager::get(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        const auto payload = client_->read(protocol::kMiConfig, 0, protocol::kConfigBytes, false, timeout_ms);
        if (payload.size() != protocol::kConfigBytes) {
            throw protocol::ProtocolError("config object is not 31 bytes");
        }
        DeviceConfig config{};
        for (size_t i = 0; i < protocol::kConfigBytes; ++i) {
            config.raw[i] = payload[i];
        }
        config.timestamp = client_->received_at();
        return config;
    }

    void ConfigManager::set_standard_id(uint16_t id, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        client_->change_request_id(id, timeout_ms);
    }

    void ConfigManager::set_extended_id(uint32_t id, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        if (id > 0x1FFFFFFFU) {
            throw ValidationError("extended CAN ID must fit in 29 bits");
        }
        std::vector<uint8_t> payload;
        protocol::append_u32_le(payload, id);
        static_cast<void>(client_->write(protocol::kMiConfig, kSiExtendedId, payload, timeout_ms));
    }

    void ConfigManager::set_control_mode(uint8_t mode, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_field(kSiControlMode, {mode}, timeout_ms);
    }

    void ConfigManager::set_led(uint8_t value, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_field(kSiLed, {value}, timeout_ms);
    }

    void ConfigManager::set_buzzer(uint8_t value, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_field(kSiBuzzer, {value}, timeout_ms);
    }

    void ConfigManager::restore_config(bool confirm, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_confirm(kSiRestoreConfig, confirm, timeout_ms);
    }

    void ConfigManager::restore_all(bool confirm, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_confirm(kSiRestoreAll, confirm, timeout_ms);
    }

    void ConfigManager::save_to_flash(bool confirm, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_confirm(kSiSaveFlash, confirm, timeout_ms);
    }

    void ConfigManager::write_field(uint8_t si, const std::vector<uint8_t> &payload,
                                    std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        if (payload.empty() || static_cast<size_t>(si) + payload.size() > protocol::kConfigBytes) {
            throw ValidationError("config write is outside the 31-byte object");
        }
        if (static_cast<size_t>(si) + payload.size() > kSiRestoreConfig) {
            throw ValidationError("use the confirmed restore/save API for config triggers");
        }
        if (si < 2) {
            if (si != 0 || payload.size() != 2) {
                throw ValidationError("standard ID must be written as one two-byte field");
            }
            set_standard_id(protocol::read_u16_le(payload.data()), timeout_ms);
            return;
        }
        static_cast<void>(client_->write(protocol::kMiConfig, si, payload, timeout_ms));
    }

    void ConfigManager::write_confirm(uint8_t si, bool confirm, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        if (!confirm) {
            throw ValidationError("refusing a destructive O30 config write without confirm=true");
        }
        static_cast<void>(client_->write(protocol::kMiConfig, si, {0x01}, timeout_ms));
    }

} // namespace linkerbot::hand::o30

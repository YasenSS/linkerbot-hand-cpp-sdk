#include "linkerbot/hand/o30/preset.hpp"
#include <linkerbot/hand/errors.hpp>

#include <stdexcept>
#include <vector>

#include <linkerbot/hand/o30/preset_manager.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    std::array<uint8_t, protocol::kPresetNonTriggerBytes> Preset::non_trigger() const {
        std::array<uint8_t, protocol::kPresetNonTriggerBytes> fields{};
        for (size_t i = 0; i < fields.size(); ++i) {
            fields[i] = raw[i];
        }
        return fields;
    }

    uint8_t PresetManager::mi_of(uint8_t index) {
        if (index < 1 || index > kPresetCount) {
            throw ValidationError("preset index must be between 1 and 5");
        }
        return static_cast<uint8_t>(protocol::kMiPreset1 + (index - 1));
    }

    PresetManager::PresetManager(Client &client) : client_(&client) {}

    Preset PresetManager::get(uint8_t index, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        const auto payload = client_->read(mi_of(index), 0, protocol::kPresetBytes, false, timeout_ms);
        if (payload.size() != protocol::kPresetBytes) {
            throw protocol::ProtocolError("preset object is not 7 bytes");
        }
        Preset preset{};
        preset.index = index;
        for (size_t i = 0; i < protocol::kPresetBytes; ++i) {
            preset.raw[i] = payload[i];
        }
        preset.timestamp = client_->received_at();
        return preset;
    }

    void PresetManager::set_non_trigger(uint8_t index,
                                        const std::array<uint8_t, protocol::kPresetNonTriggerBytes> &fields,
                                        std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        static_cast<void>(
            client_->write(mi_of(index), 0, std::vector<uint8_t>(fields.begin(), fields.end()), timeout_ms));
    }

    void PresetManager::set_raw(uint8_t index, const std::array<uint8_t, protocol::kPresetBytes> &raw,
                                std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        static_cast<void>(client_->write(mi_of(index), 0, std::vector<uint8_t>(raw.begin(), raw.end()), timeout_ms));
    }

} // namespace linkerbot::hand::o30

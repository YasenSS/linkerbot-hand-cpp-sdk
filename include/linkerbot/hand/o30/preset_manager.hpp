#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/preset.hpp>

namespace linkerbot::hand::o30 {

    class PresetManager {
        friend class O30;

    public:
        explicit PresetManager(Client &client);

        PresetManager(const PresetManager &) = delete;
        PresetManager &operator=(const PresetManager &) = delete;
        PresetManager(PresetManager &&) = delete;
        PresetManager &operator=(PresetManager &&) = delete;

        // index is 1..5 (MI 0x36..0x3A).
        [[nodiscard]] Preset get(uint8_t index, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        void set_non_trigger(uint8_t index, const std::array<uint8_t, protocol::kPresetNonTriggerBytes> &fields,
                             std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        void set_raw(uint8_t index, const std::array<uint8_t, protocol::kPresetBytes> &raw,
                     std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

    private:
        [[nodiscard]] static uint8_t mi_of(uint8_t index);

        Client *client_;
    };

} // namespace linkerbot::hand::o30

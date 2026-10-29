#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/mapping.hpp>

namespace linkerbot::hand::o30 {

    class MappingManager {
        friend class O30;

    public:
        explicit MappingManager(Client &client);

        MappingManager(const MappingManager &) = delete;
        MappingManager &operator=(const MappingManager &) = delete;
        MappingManager(MappingManager &&) = delete;
        MappingManager &operator=(MappingManager &&) = delete;

        // Reads the 36-byte map at SI 0x00. Current firmware does not answer.
        [[nodiscard]] std::array<uint8_t, kSlotCount>
        get_map(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        // SI 0x24 length + SI 0x25 enable, 2 bytes.
        [[nodiscard]] JointMapping
        get_length_and_enable(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        void set_map(const std::array<uint8_t, kSlotCount> &map,
                     std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_length_and_enable(uint8_t length, uint8_t enabled,
                                   std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

    private:
        Client *client_;
    };

} // namespace linkerbot::hand::o30

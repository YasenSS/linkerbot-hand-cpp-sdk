#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/unit.hpp>

namespace linkerbot::hand::o30 {

    class UnitManager {
        friend class O30;

    public:
        explicit UnitManager(Client &client);

        UnitManager(const UnitManager &) = delete;
        UnitManager &operator=(const UnitManager &) = delete;
        UnitManager(UnitManager &&) = delete;
        UnitManager &operator=(UnitManager &&) = delete;

        void select_target(uint8_t mi, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] UnitInfo get_unit(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

        // SI is the start of a 4-byte little-endian low/high pair.
        [[nodiscard]] RangeInfo get_range(uint8_t si,
                                          std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] UnitInfo query(uint8_t mi, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

    private:
        std::recursive_mutex mutex_;
        Client *client_;
        uint8_t target_mi_ = 0;
    };

} // namespace linkerbot::hand::o30

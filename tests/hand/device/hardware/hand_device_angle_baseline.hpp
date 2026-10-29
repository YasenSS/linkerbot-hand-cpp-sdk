#pragma once

#include "hand_device_hardware_fixture.hpp"
#include <linkerbot/hand/o30/o30.hpp>

namespace linkerbot::hand::hardware_test {
    // O30 full-vector commands must preserve configured targets rather than
    // replace them with measured positions that may have tracking offsets.
    inline std::optional<std::vector<int32_t>> o30_setpoint_pose(const HandDeviceConfig &config, o30::O30 *reader) {
        if (config.model != Model::kO30) {
            return std::nullopt;
        }
        if (!reader) {
            throw std::invalid_argument("O30 baseline requires the fixture's shared reader");
        }
        const auto targets =
            o30::project_u8(reader->angle.get_slots(std::chrono::seconds{1}, o30::ValueKind::kSetpoint));
        return std::vector<int32_t>(targets.begin(), targets.end());
    }
}

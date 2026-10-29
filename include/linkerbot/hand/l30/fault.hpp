#pragma once

#include <linkerbot/hand/fault_status.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include <linkerbot/hand/l30/joints.hpp>

namespace linkerbot::hand::l30 {

    // Bit positions inside one joint's fault byte (section 10.3). Bit 4 and bits
    // 6-7 are reserved. A set bit means the condition is abnormal.
    inline constexpr uint8_t kFaultVoltageBit = 0;
    inline constexpr uint8_t kFaultMagneticEncoderBit = 1;
    inline constexpr uint8_t kFaultTemperatureBit = 2;
    inline constexpr uint8_t kFaultCurrentBit = 3;
    inline constexpr uint8_t kFaultLoadBit = 5;

    // Raw per-joint fault bytes plus accessors for the documented bits.
    struct FaultData {
        std::array<uint8_t, kJointCount> faults{};
        std::chrono::steady_clock::time_point timestamp;

        [[nodiscard]] FaultStatus status(size_t joint_index) const {
            return decode_fault_status(faults.at(joint_index), 0x2F);
        }

        [[nodiscard]] bool has_voltage_fault(size_t joint_index) const {
            return has_bit(joint_index, kFaultVoltageBit);
        }

        [[nodiscard]] bool has_magnetic_encoder_fault(size_t joint_index) const {
            return has_bit(joint_index, kFaultMagneticEncoderBit);
        }

        [[nodiscard]] bool has_temperature_fault(size_t joint_index) const {
            return has_bit(joint_index, kFaultTemperatureBit);
        }

        [[nodiscard]] bool has_current_fault(size_t joint_index) const {
            return has_bit(joint_index, kFaultCurrentBit);
        }

        [[nodiscard]] bool has_load_fault(size_t joint_index) const { return has_bit(joint_index, kFaultLoadBit); }

        // True when any documented fault bit is set for the joint.
        [[nodiscard]] bool has_any_fault(size_t joint_index) const {
            return has_voltage_fault(joint_index) || has_magnetic_encoder_fault(joint_index) ||
                   has_temperature_fault(joint_index) || has_current_fault(joint_index) || has_load_fault(joint_index);
        }

    private:
        [[nodiscard]] bool has_bit(size_t joint_index, uint8_t bit) const {
            return (faults.at(joint_index) & static_cast<uint8_t>(1U << bit)) != 0U;
        }
    };

} // namespace linkerbot::hand::l30

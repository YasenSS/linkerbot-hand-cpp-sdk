#pragma once

#include <linkerbot/hand/fault_status.hpp>

#include <array>
#include <chrono>
#include <cstdint>

namespace linkerbot::hand::l6 {

    enum class FaultCode : uint8_t {
        kNone = 0x00,
        kPhaseBOvercurrent = 0x01,
        kPhaseCOvercurrent = 0x02,
        kPhaseAOvercurrent = 0x04,
        kOverload1 = 0x08,
        kOverload2 = 0x10,
        kMotorOvertemp = 0x20,
        kMcuOvertemp = 0x40,
    };

    constexpr FaultCode operator|(FaultCode lhs, FaultCode rhs) {
        return static_cast<FaultCode>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    constexpr FaultCode operator&(FaultCode lhs, FaultCode rhs) {
        return static_cast<FaultCode>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
    }

    constexpr FaultCode operator~(FaultCode code) { return static_cast<FaultCode>(~static_cast<uint8_t>(code)); }

    constexpr bool has_fault(FaultCode code) { return (static_cast<uint8_t>(code) & 0x7F) != 0; }

    struct Fault {
        static constexpr size_t kJointCount = 6;

        FaultCode thumb_flex = FaultCode::kNone;
        FaultCode thumb_abd = FaultCode::kNone;
        FaultCode index = FaultCode::kNone;
        FaultCode middle = FaultCode::kNone;
        FaultCode ring = FaultCode::kNone;
        FaultCode pinky = FaultCode::kNone;

        Fault() = default;
        Fault(FaultCode tf, FaultCode ta, FaultCode i, FaultCode m, FaultCode r, FaultCode p)
            : thumb_flex(tf), thumb_abd(ta), index(i), middle(m), ring(r), pinky(p) {}

        [[nodiscard]] std::array<FaultCode, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index, middle, ring, pinky};
        }

        [[nodiscard]] static Fault from_array(const std::array<FaultCode, kJointCount> &values) {
            return {values[0], values[1], values[2], values[3], values[4], values[5]};
        }

        [[nodiscard]] static Fault from_raw(const std::array<uint8_t, kJointCount> &values) {
            return {static_cast<FaultCode>(values[0]), static_cast<FaultCode>(values[1]),
                    static_cast<FaultCode>(values[2]), static_cast<FaultCode>(values[3]),
                    static_cast<FaultCode>(values[4]), static_cast<FaultCode>(values[5])};
        }

        [[nodiscard]] std::array<uint8_t, kJointCount> to_raw() const {
            const auto codes = to_array();
            std::array<uint8_t, kJointCount> raw{};
            for (size_t i = 0; i < kJointCount; ++i) {
                raw[i] = static_cast<uint8_t>(codes[i]);
            }
            return raw;
        }

        [[nodiscard]] FaultStatus status(size_t joint_index) const {
            return decode_fault_status(to_raw().at(joint_index), 0x7F);
        }

        [[nodiscard]] bool has_any_fault() const {
            return has_fault(thumb_flex) || has_fault(thumb_abd) || has_fault(index) || has_fault(middle) ||
                   has_fault(ring) || has_fault(pinky);
        }
    };

    struct FaultData {
        Fault fault;
        std::chrono::steady_clock::time_point timestamp;

        [[nodiscard]] FaultStatus status(size_t joint_index) const { return fault.status(joint_index); }
    };

} // namespace linkerbot::hand::l6

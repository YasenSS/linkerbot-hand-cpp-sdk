#pragma once

#include <linkerbot/hand/fault_status.hpp>

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/l20lite/joints.hpp>

namespace linkerbot::hand::l20lite {

    enum class FaultCode : uint8_t {
        kNone = 0x00,
        kVoltage = 0x01,
        kEncoder = 0x02,
        kOvertemp = 0x04,
        kOvercurrent = 0x08,
        kOverload = 0x20,
    };

    constexpr FaultCode operator|(FaultCode lhs, FaultCode rhs) {
        // Fixed-underlying-type enum values are valid C++17, including unknown values and bit masks.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        return static_cast<FaultCode>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    constexpr FaultCode operator&(FaultCode lhs, FaultCode rhs) {
        return static_cast<FaultCode>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
    }

    constexpr bool has_fault(FaultCode code) { return (static_cast<uint8_t>(code) & 0x2F) != 0; }

    struct Fault {
        static constexpr size_t kJointCount = l20lite::kJointCount;
        static constexpr uint8_t kDefinedBits = 0x2F;

        FaultCode thumb_flex = FaultCode::kNone;
        FaultCode thumb_abd = FaultCode::kNone;
        FaultCode index_flex = FaultCode::kNone;
        FaultCode middle_flex = FaultCode::kNone;
        FaultCode ring_flex = FaultCode::kNone;
        FaultCode pinky_flex = FaultCode::kNone;
        FaultCode index_abd = FaultCode::kNone;
        FaultCode ring_abd = FaultCode::kNone;
        FaultCode pinky_abd = FaultCode::kNone;
        FaultCode thumb_yaw = FaultCode::kNone;

        Fault() = default;
        Fault(FaultCode tf, FaultCode ta, FaultCode ix, FaultCode mf, FaultCode rf, FaultCode pf, FaultCode ia,
              FaultCode ra, FaultCode pa, FaultCode ty)
            : Fault(from_fields({tf, ta, ix, mf, rf, pf, ia, ra, pa, ty})) {}

        [[nodiscard]] std::array<FaultCode, kJointCount> to_array() const {
            return {thumb_flex, thumb_abd, index_flex, middle_flex, ring_flex,
                    pinky_flex, index_abd, ring_abd,   pinky_abd,   thumb_yaw};
        }

        [[nodiscard]] static Fault from_array(const std::array<FaultCode, kJointCount> &values) {
            return from_fields(values);
        }

        [[nodiscard]] static Fault from_raw(const std::array<uint8_t, kJointCount> &values) {
            std::array<FaultCode, kJointCount> codes{};
            for (size_t i = 0; i < kJointCount; ++i) {
                codes[i] = static_cast<FaultCode>(values[i] & kDefinedBits);
            }
            auto fault = from_array(codes);
            for (size_t i = 0; i < kJointCount; ++i) {
                fault.reserved_bits_[i] = static_cast<uint8_t>(values[i] & ~kDefinedBits);
            }
            return fault;
        }

        [[nodiscard]] std::array<uint8_t, kJointCount> to_raw() const {
            const auto codes = to_array();
            std::array<uint8_t, kJointCount> raw{};
            for (size_t i = 0; i < kJointCount; ++i) {
                raw[i] = static_cast<uint8_t>(codes[i]) | reserved_bits_[i];
            }
            return raw;
        }

        [[nodiscard]] FaultStatus status(size_t joint_index) const {
            return decode_fault_status(to_raw().at(joint_index), 0x2F);
        }

        [[nodiscard]] bool has_any_fault() const {
            for (auto code : to_array()) {
                if (has_fault(code)) {
                    return true;
                }
            }
            return false;
        }

    private:
        std::array<uint8_t, kJointCount> reserved_bits_{};
        static Fault from_fields(const std::array<FaultCode, kJointCount> &values) {
            Fault result;
            result.thumb_flex = values[0];
            result.thumb_abd = values[1];
            result.index_flex = values[2];
            result.middle_flex = values[3];
            result.ring_flex = values[4];
            result.pinky_flex = values[5];
            result.index_abd = values[6];
            result.ring_abd = values[7];
            result.pinky_abd = values[8];
            result.thumb_yaw = values[9];
            return result;
        }
    };

    struct FaultData {
        Fault fault;
        std::chrono::steady_clock::time_point timestamp;

        [[nodiscard]] FaultStatus status(size_t joint_index) const { return fault.status(joint_index); }
    };

} // namespace linkerbot::hand::l20lite

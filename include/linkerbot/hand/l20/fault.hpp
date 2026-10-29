#pragma once

#include <linkerbot/hand/fault_status.hpp>

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/l20/joints.hpp>

namespace linkerbot::hand::l20 {

    // L20 fault codes are an 8-bit bit-field per joint. Multiple bits may be
    // set simultaneously; combine and inspect with the bitwise operators below.
    enum class L20FaultCode : uint8_t {
        kNone = 0x00,
        kMotorRotorLock = 0x01,
        kMotorOverCurrent = 0x02,
        kMotorStallFault = 0x04,
        kVoltageAbnormal = 0x08,
        kSelfCheckAbnormal = 0x10,
        kOverTemperature = 0x20,
        kSoftRotorLock = 0x40,
        kMotorCommAbnormal = 0x80,
    };

    constexpr L20FaultCode operator|(L20FaultCode lhs, L20FaultCode rhs) {
        return static_cast<L20FaultCode>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    constexpr L20FaultCode operator&(L20FaultCode lhs, L20FaultCode rhs) {
        return static_cast<L20FaultCode>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
    }

    constexpr bool has_fault(L20FaultCode code) { return code != L20FaultCode::kNone; }

    struct Fault {
        static constexpr size_t kJointCount = l20::kJointCount;

        L20FaultCode thumb_abd = L20FaultCode::kNone;
        L20FaultCode thumb_yaw = L20FaultCode::kNone;
        L20FaultCode thumb_root1 = L20FaultCode::kNone;
        L20FaultCode thumb_tip = L20FaultCode::kNone;
        L20FaultCode index_abd = L20FaultCode::kNone;
        L20FaultCode index_root1 = L20FaultCode::kNone;
        L20FaultCode index_tip = L20FaultCode::kNone;
        L20FaultCode middle_abd = L20FaultCode::kNone;
        L20FaultCode middle_root1 = L20FaultCode::kNone;
        L20FaultCode middle_tip = L20FaultCode::kNone;
        L20FaultCode ring_abd = L20FaultCode::kNone;
        L20FaultCode ring_root1 = L20FaultCode::kNone;
        L20FaultCode ring_tip = L20FaultCode::kNone;
        L20FaultCode pinky_abd = L20FaultCode::kNone;
        L20FaultCode pinky_root1 = L20FaultCode::kNone;
        L20FaultCode pinky_tip = L20FaultCode::kNone;

        Fault() = default;
        Fault(L20FaultCode t_abd, L20FaultCode t_yaw, L20FaultCode t_root, L20FaultCode t_tip, L20FaultCode i_abd,
              L20FaultCode i_root, L20FaultCode i_tip, L20FaultCode m_abd, L20FaultCode m_root, L20FaultCode m_tip,
              L20FaultCode r_abd, L20FaultCode r_root, L20FaultCode r_tip, L20FaultCode p_abd, L20FaultCode p_root,
              L20FaultCode p_tip)
            : thumb_abd(t_abd), thumb_yaw(t_yaw), thumb_root1(t_root), thumb_tip(t_tip), index_abd(i_abd),
              index_root1(i_root), index_tip(i_tip), middle_abd(m_abd), middle_root1(m_root), middle_tip(m_tip),
              ring_abd(r_abd), ring_root1(r_root), ring_tip(r_tip), pinky_abd(p_abd), pinky_root1(p_root),
              pinky_tip(p_tip) {}

        [[nodiscard]] std::array<L20FaultCode, kJointCount> to_array() const {
            return {thumb_abd,    thumb_yaw,  thumb_root1, thumb_tip,  index_abd, index_root1, index_tip,   middle_abd,
                    middle_root1, middle_tip, ring_abd,    ring_root1, ring_tip,  pinky_abd,   pinky_root1, pinky_tip};
        }

        [[nodiscard]] static Fault from_array(const std::array<L20FaultCode, kJointCount> &values) {
            return {values[0], values[1], values[2],  values[3],  values[4],  values[5],  values[6],  values[7],
                    values[8], values[9], values[10], values[11], values[12], values[13], values[14], values[15]};
        }

        [[nodiscard]] static Fault from_raw(const std::array<uint8_t, kJointCount> &values) {
            std::array<L20FaultCode, kJointCount> codes{};
            for (size_t i = 0; i < kJointCount; ++i) {
                codes[i] = static_cast<L20FaultCode>(values[i]);
            }
            return from_array(codes);
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
            return decode_fault_status(to_raw().at(joint_index), 0xFF);
        }

        [[nodiscard]] bool has_any_fault() const {
            for (auto code : to_array()) {
                if (has_fault(code)) {
                    return true;
                }
            }
            return false;
        }
    };

    struct FaultData {
        Fault fault;
        std::chrono::steady_clock::time_point timestamp;

        [[nodiscard]] FaultStatus status(size_t joint_index) const { return fault.status(joint_index); }
    };

} // namespace linkerbot::hand::l20

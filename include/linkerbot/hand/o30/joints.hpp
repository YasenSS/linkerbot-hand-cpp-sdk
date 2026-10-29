#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace linkerbot::hand::o30 {

    // HOP objects are 36 slots wide. O30 only mounts 20 of them; the rest are
    // reserved and must not be written (see kSafeWriteSegments).
    inline constexpr size_t kSlotCount = 36;
    inline constexpr size_t kJointCount = 20;

    // Slot index of each assembled joint, in the SDK's canonical order.
    //
    //   0      SI 0x00  thumb rotation (protocol roll_0)
    //   1      SI 0x05  thumb roll (protocol yaw_0)
    //   2..5   SI 0x06  four-finger abduction (protocol yaw_1..yaw_4)
    //   6..10  SI 0x0A  five-finger root 1
    //   11..14 SI 0x10  index..pinky root 2 (no thumb root 2 at SI 0x0F)
    //   15..19 SI 0x14  five fingertips
    inline constexpr std::array<uint8_t, kJointCount> kJointSlots{{
        0x00, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
        0x0E, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    }};

    inline constexpr std::array<std::string_view, kJointCount> kJointNames{{
        "thumb_rotation", "thumb_roll",   "index_abd",  "middle_abd",  "ring_abd",    "pinky_abd",    "thumb_root1",
        "index_root1",    "middle_root1", "ring_root1", "pinky_root1", "index_root2", "middle_root2", "ring_root2",
        "pinky_root2",    "thumb_tip",    "index_tip",  "middle_tip",  "ring_tip",    "pinky_tip",
    }};

    // Physical endpoints from the measured direction summary (2026-08-12),
    // section O30i. Protocol names are labels, not physical axis definitions.
    // Both hands use these same raw endpoints: min_pose is raw 0x00 and max_pose
    // is raw 0xFF. Public percent reverses flexion to match Python: 0% flexed,
    // 100% extended. Other axes interpolate from min_pose to max_pose.
    struct JointSemantics {
        std::string_view protocol_name;
        std::string_view min_pose;
        std::string_view max_pose;
    };

    inline constexpr std::array<JointSemantics, kJointCount> kJointSemantics{{
        {"roll_0", "away_from_index", "toward_index"},
        {"yaw_0", "parallel_to_palm", "perpendicular_to_palm"},
        {"yaw_1", "toward_thumb", "away_from_thumb"},
        {"yaw_2", "toward_thumb", "away_from_thumb"},
        {"yaw_3", "toward_thumb", "away_from_thumb"},
        {"yaw_4", "toward_thumb", "away_from_thumb"},
        {"root1_0", "extended", "flexed"},
        {"root1_1", "extended", "flexed"},
        {"root1_2", "extended", "flexed"},
        {"root1_3", "extended", "flexed"},
        {"root1_4", "extended", "flexed"},
        {"root2_1", "extended", "flexed"},
        {"root2_2", "extended", "flexed"},
        {"root2_3", "extended", "flexed"},
        {"root2_4", "extended", "flexed"},
        {"tip_0", "extended", "flexed"},
        {"tip_1", "extended", "flexed"},
        {"tip_2", "extended", "flexed"},
        {"tip_3", "extended", "flexed"},
        {"tip_4", "extended", "flexed"},
    }};

    // MI 0x30 numbers joints finger-major, not by the dense SI layout.
    // Thumb tip is 0x04 and index tip is 0x09 (protocol sparse example).
    inline constexpr std::array<uint8_t, kJointCount> kJointSparseIds{{
        0, 1, 6, 11, 16, 21, 2, 7, 12, 17, 22, 8, 13, 18, 23, 4, 9, 14, 19, 24,
    }};

    // Consecutive mounted ranges used when writing a full 20-joint vector.
    // Unmounted slots (0x01-0x04, 0x0F, 0x19-0x23) are skipped on purpose.
    struct WriteSegment {
        uint8_t si;
        uint8_t edl;
    };

    inline constexpr std::array<WriteSegment, 3> kSafeWriteSegments{{
        {0x00, 1},
        {0x05, 10},
        {0x10, 9},
    }};

    inline constexpr uint8_t kThumbRotationSi = 0x00;
    inline constexpr uint8_t kThumbRollSi = 0x05;
    // Protocol yaw group includes thumb roll, followed by four-finger abduction.
    inline constexpr uint8_t kYawSi = 0x05;
    inline constexpr uint8_t kRoot1Si = 0x0A;
    inline constexpr uint8_t kRoot2Si = 0x10;
    inline constexpr uint8_t kTipSi = 0x14;

    inline constexpr uint8_t kYawCount = 5;
    inline constexpr uint8_t kRoot1Count = 5;
    inline constexpr uint8_t kRoot2Count = 4;
    inline constexpr uint8_t kTipCount = 5;

    inline constexpr size_t kYawJointBegin = 1;
    inline constexpr size_t kRoot1JointBegin = 6;
    inline constexpr size_t kRoot2JointBegin = 11;
    inline constexpr size_t kTipJointBegin = 15;

    // Actual vs setpoint. CTRL bit7 is RTS; kSetpoint sends MI | 0x80.
    enum class ValueKind { kActual, kSetpoint };

    [[nodiscard]] constexpr std::string_view joint_name(size_t index) { return kJointNames.at(index); }

    [[nodiscard]] constexpr uint8_t joint_slot(size_t index) { return kJointSlots.at(index); }

    [[nodiscard]] constexpr uint8_t joint_sparse_id(size_t index) { return kJointSparseIds.at(index); }

    // Inverse of kJointSlots. Returns kSlotCount when the slot is unmounted.
    [[nodiscard]] size_t slot_to_joint(uint8_t slot);

    [[nodiscard]] std::array<uint8_t, kJointCount> project_u8(const std::array<uint8_t, kSlotCount> &slots);
    [[nodiscard]] std::array<uint8_t, kSlotCount> expand_u8(const std::array<uint8_t, kJointCount> &joints);

    [[nodiscard]] std::array<uint16_t, kJointCount> project_u16(const std::array<uint16_t, kSlotCount> &slots);
    [[nodiscard]] std::array<uint16_t, kSlotCount> expand_u16(const std::array<uint16_t, kJointCount> &joints);

    // Flexion helper: 0 % -> raw 0xFF, 100 % -> raw 0x00.
    // Use indexed helpers for rotation, roll and abduction (increasing raw).
    // Finite out-of-range helper inputs clamp;
    // command APIs reject out-of-range percentages. Nonfinite inputs throw.
    [[nodiscard]] uint8_t percent_to_raw(float percent);
    [[nodiscard]] float raw_to_percent(uint8_t raw);

    // Canonical SDK joint indices 0..19, unchanged slot ordering. Invalid
    // indices throw. Direction is identical for left and right hands.
    [[nodiscard]] uint8_t joint_percent_to_raw(size_t joint_index, float percent);
    [[nodiscard]] float joint_raw_to_percent(size_t joint_index, uint8_t raw);

    // Per-joint conversion in the canonical 20-joint SDK order.
    [[nodiscard]] std::array<uint8_t, kJointCount> percents_to_raw(const std::array<float, kJointCount> &percents);
    [[nodiscard]] std::array<float, kJointCount> raw_to_percents(const std::array<uint8_t, kJointCount> &raw);

    void validate_percents(const std::array<float, kJointCount> &percents, std::string_view label);

    void validate_u8_count(size_t count, size_t expected, std::string_view label);

} // namespace linkerbot::hand::o30

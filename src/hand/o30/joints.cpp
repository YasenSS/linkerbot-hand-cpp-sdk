#include "linkerbot/hand/o30/joints.hpp"
#include <linkerbot/hand/errors.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace linkerbot::hand::o30 {

    size_t slot_to_joint(uint8_t slot) {
        for (size_t i = 0; i < kJointCount; ++i) {
            if (kJointSlots[i] == slot) {
                return i;
            }
        }
        return kSlotCount;
    }

    std::array<uint8_t, kJointCount> project_u8(const std::array<uint8_t, kSlotCount> &slots) {
        std::array<uint8_t, kJointCount> joints{};
        for (size_t i = 0; i < kJointCount; ++i) {
            joints[i] = slots[kJointSlots[i]];
        }
        return joints;
    }

    std::array<uint8_t, kSlotCount> expand_u8(const std::array<uint8_t, kJointCount> &joints) {
        std::array<uint8_t, kSlotCount> slots{};
        for (size_t i = 0; i < kJointCount; ++i) {
            slots[kJointSlots[i]] = joints[i];
        }
        return slots;
    }

    std::array<uint16_t, kJointCount> project_u16(const std::array<uint16_t, kSlotCount> &slots) {
        std::array<uint16_t, kJointCount> joints{};
        for (size_t i = 0; i < kJointCount; ++i) {
            joints[i] = slots[kJointSlots[i]];
        }
        return joints;
    }

    std::array<uint16_t, kSlotCount> expand_u16(const std::array<uint16_t, kJointCount> &joints) {
        std::array<uint16_t, kSlotCount> slots{};
        for (size_t i = 0; i < kJointCount; ++i) {
            slots[kJointSlots[i]] = joints[i];
        }
        return slots;
    }

    namespace {
        uint8_t scaled_raw(double percent) {
            if (!std::isfinite(percent)) {
                throw ValidationError("O30 percentage must be finite");
            }
            if (percent <= 0) {
                return 0;
            }
            if (percent >= 100) {
                return 255;
            }
            return static_cast<uint8_t>(std::lround(percent * 255.0 / 100.0));
        }
    } // namespace

    uint8_t percent_to_raw(float percent) { return scaled_raw(100.0 - static_cast<double>(percent)); }

    float raw_to_percent(uint8_t raw) { return static_cast<float>(255 - raw) * 100.0F / 255.0F; }

    uint8_t joint_percent_to_raw(size_t joint_index, float percent) {
        if (joint_index >= kJointCount) {
            throw ValidationError("O30 joint index is outside 0..19");
        }
        return joint_index >= kRoot1JointBegin ? percent_to_raw(percent) : scaled_raw(static_cast<double>(percent));
    }

    float joint_raw_to_percent(size_t joint_index, uint8_t raw) {
        if (joint_index >= kJointCount) {
            throw ValidationError("O30 joint index is outside 0..19");
        }
        return joint_index >= kRoot1JointBegin ? raw_to_percent(raw) : static_cast<float>(raw) * 100.0F / 255.0F;
    }

    std::array<uint8_t, kJointCount> percents_to_raw(const std::array<float, kJointCount> &percents) {
        std::array<uint8_t, kJointCount> raw{};
        for (size_t i = 0; i < kJointCount; ++i) {
            raw[i] = joint_percent_to_raw(i, percents[i]);
        }
        return raw;
    }

    std::array<float, kJointCount> raw_to_percents(const std::array<uint8_t, kJointCount> &raw) {
        std::array<float, kJointCount> percents{};
        for (size_t i = 0; i < kJointCount; ++i) {
            percents[i] = joint_raw_to_percent(i, raw[i]);
        }
        return percents;
    }

    void validate_percents(const std::array<float, kJointCount> &percents, std::string_view label) {
        for (size_t i = 0; i < kJointCount; ++i) {
            if (!(percents[i] >= 0.0F) || !(percents[i] <= 100.0F)) {
                throw ValidationError(std::string(label) + "[" + std::to_string(i) +
                                      "] must be between 0 and 100, got " + std::to_string(percents[i]));
            }
        }
    }

    void validate_u8_count(size_t count, size_t expected, std::string_view label) {
        if (count != expected) {
            throw ValidationError(std::string(label) + " expects " + std::to_string(expected) + " values, got " +
                                  std::to_string(count));
        }
    }

} // namespace linkerbot::hand::o30

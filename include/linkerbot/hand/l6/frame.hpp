#pragma once

#include <cstdint>

#include <linkerbot/can/message.hpp>

namespace linkerbot::hand::l6 {
    constexpr uint32_t kLeftArbitrationId = 0x28;
    constexpr uint32_t kRightArbitrationId = 0x27;

    constexpr uint8_t kAngleOpcode = 0x01;
    constexpr uint8_t kCurrentLimitOpcode = 0x02;
    constexpr uint8_t kTorqueOpcode = 0x03;
    constexpr uint8_t kSpeedOpcode = 0x05;
    constexpr uint8_t kPositionKpOpcode = 0x06;
    constexpr uint8_t kPositionKdOpcode = 0x07;
    constexpr uint8_t kTorqueKdOpcode = 0x08;
    constexpr uint8_t kLimitCompensationOpcode = 0x38;
    constexpr uint8_t kTemperatureOpcode = 0x33;
    constexpr uint8_t kFaultOpcode = 0x35;
    constexpr uint8_t kCurrentOpcode = 0x36;
    constexpr uint8_t kClearFaultOpcode = 0x83;
    constexpr uint8_t kThumbForceOpcode = 0xB1;
    constexpr uint8_t kIndexForceOpcode = 0xB2;
    constexpr uint8_t kMiddleForceOpcode = 0xB3;
    constexpr uint8_t kRingForceOpcode = 0xB4;
    constexpr uint8_t kPinkyForceOpcode = 0xB5;
    constexpr uint8_t kForceSelectByte = 0xC6;
    constexpr uint8_t kSerialOpcode = 0xC0;
    constexpr uint8_t kPcbVersionOpcode = 0xC1;
    constexpr uint8_t kFirmwareVersionOpcode = 0xC2;
    constexpr uint8_t kSetIdOpcode = 0xC3;
    constexpr uint8_t kMechanicalVersionOpcode = 0xC4;
    constexpr uint8_t kStallTimeOpcode = 0xC5;
    constexpr uint8_t kStallThresholdOpcode = 0xC6;
    constexpr uint8_t kStallTorqueOpcode = 0xC7;
    constexpr uint8_t kReplyOpcode = 0xCC;
    constexpr uint8_t kFactoryOpcode = 0xCE;
    constexpr uint8_t kSaveOpcode = 0xCF;

    [[nodiscard]] inline bool is_classic_can(const can::Message &msg) {
        return !msg.is_extended && !msg.is_fd && !msg.is_brs;
    }

    [[nodiscard]] inline bool is_classic_frame(const can::Message &msg, uint32_t arbitration_id) {
        return msg.id == arbitration_id && is_classic_can(msg);
    }

    [[nodiscard]] inline bool is_classic_response(const can::Message &msg, uint32_t arbitration_id, uint8_t dlc,
                                                  uint8_t opcode) {
        return is_classic_frame(msg, arbitration_id) && msg.dlc == dlc && dlc > 0 && msg.data[0] == opcode;
    }
} // namespace linkerbot::hand::l6

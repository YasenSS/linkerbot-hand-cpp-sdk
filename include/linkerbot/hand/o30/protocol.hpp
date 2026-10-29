#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/o30/joints.hpp>

// O30i HOP over CAN FD standard frames. Revision-dependent addresses must
// be selected from the device version, never inferred from joint values.
//
// Byte0 CTRL = (RTS << 7) | MI
// Byte1 SI   = sub-index / start offset
// Byte2 EDL  = effective payload length
// Byte3..    = payload (writes only; reads carry EDL but no data)
//
// CAN FD DLC may be 48 or 64; useful bytes stop at 3 + EDL. A single HOP
// fragment holds at most 61 payload bytes; longer objects arrive as SI-chained
// fragments and must be collected before the next request is sent.
namespace linkerbot::hand::o30::protocol {
    enum class Revision { kV003, kV004 };

    inline constexpr uint32_t kDefaultRequestId = 0x001;
    inline constexpr uint32_t kResponseIdOffset = 0x400;

    inline constexpr uint8_t kCtrlMiMask = 0x7F;
    inline constexpr uint8_t kCtrlRtsBit = 0x80;

    inline constexpr size_t kHeaderBytes = 3;
    inline constexpr size_t kMaxHopPayload = 61;
    inline constexpr size_t kMaxHopFrame = kHeaderBytes + kMaxHopPayload;

    inline constexpr uint8_t kMiMapping = 0x00;
    inline constexpr uint8_t kMiPosition = 0x01;
    inline constexpr uint8_t kMiSpeed = 0x02;
    inline constexpr uint8_t kMiAcceleration = 0x03;
    inline constexpr uint8_t kMiCurrent = 0x04;
    inline constexpr uint8_t kMiVoltage = 0x05;
    inline constexpr uint8_t kMiTorque = 0x06;
    inline constexpr uint8_t kMiTemperature = 0x07;
    inline constexpr uint8_t kMiStallTime = 0x09;
    inline constexpr uint8_t kMiStallThreshold = 0x0A;
    inline constexpr uint8_t kMiStallHoldCurrent = 0x0B;
    inline constexpr uint8_t kMiFaultV003 = 0x0D;
    inline constexpr uint8_t kMiValidJointV003 = 0x0C;
    inline constexpr uint8_t kMiFaultV004 = 0x0C;
    inline constexpr uint8_t kMiValidJointV004 = 0x0D;
    // Retained 0.0.4 aliases. Managers select addresses using Revision.
    inline constexpr uint8_t kMiFault = kMiFaultV004;
    inline constexpr uint8_t kMiValidJoint = kMiValidJointV004;
    inline constexpr uint8_t kMiPosition16 = 0x20;
    inline constexpr uint8_t kMiPositionPidP = 0x21;
    inline constexpr uint8_t kMiPositionPidI = 0x22;
    inline constexpr uint8_t kMiPositionPidD = 0x23;
    inline constexpr uint8_t kMiSpeedPidP = 0x24;
    inline constexpr uint8_t kMiSpeedPidI = 0x25;
    inline constexpr uint8_t kMiSpeedPidD = 0x26;
    inline constexpr uint8_t kMiSparsePosition = 0x30;
    inline constexpr uint8_t kMiSensorInfo = 0x31;
    inline constexpr uint8_t kMiSensorCh1 = 0x32;
    inline constexpr uint8_t kMiSensorCh2 = 0x33;
    inline constexpr uint8_t kMiSensorCh3 = 0x34;
    inline constexpr uint8_t kMiConfig = 0x35;
    inline constexpr uint8_t kMiPreset1 = 0x36;
    inline constexpr uint8_t kMiPreset2 = 0x37;
    inline constexpr uint8_t kMiPreset3 = 0x38;
    inline constexpr uint8_t kMiPreset4 = 0x39;
    inline constexpr uint8_t kMiPreset5 = 0x3A;
    inline constexpr uint8_t kMiProductInfo = 0x41;
    inline constexpr uint8_t kMiService = 0x42;
    inline constexpr uint8_t kMiUnit = 0x43;
    inline constexpr uint8_t kMiCommError = 0x4F;

    inline constexpr uint8_t kU8ObjectBytes = 36;
    inline constexpr uint8_t kU16ObjectBytes = 72;
    inline constexpr uint8_t kProductInfoBytes = 225;
    inline constexpr uint8_t kConfigBytes = 31;
    inline constexpr uint8_t kPresetBytes = 7;
    inline constexpr uint8_t kPresetNonTriggerBytes = 5;
    inline constexpr uint8_t kErrorHistoryBytes = 16;

    inline constexpr uint8_t kErrorSubIndexMissing = 0x02;
    inline constexpr uint8_t kErrorIllegalValue = 0x40;

    inline constexpr uint8_t kEnableExists = 0x01;
    inline constexpr uint8_t kEnableControllable = 0x02;
    inline constexpr uint8_t kEnableOn = 0x04;
    inline constexpr uint8_t kEnableFeedback = 0x08;
    inline constexpr uint8_t kEnableCalibrate = 0x10;

    class ProtocolError : public ::linkerbot::hand::ProtocolError {
    public:
        explicit ProtocolError(const std::string &message, uint8_t error_code = 0)
            : ::linkerbot::hand::ProtocolError(message, protocol_context(error_code)), error_code_(error_code) {}

        [[nodiscard]] uint8_t error_code() const { return error_code_; }

    private:
        static ErrorContext protocol_context(uint8_t code) {
            ErrorContext context;
            if (code != 0) {
                context.native_status = code;
            }
            return context;
        }
        uint8_t error_code_;
    };

    struct HopFrame {
        uint8_t mi = 0;
        bool rts = false;
        uint8_t si = 0;
        uint8_t edl = 0;
        std::vector<uint8_t> payload;
    };

    [[nodiscard]] uint8_t ctrl_byte(uint8_t mi, bool rts);
    [[nodiscard]] uint8_t mi_of(uint8_t ctrl);
    [[nodiscard]] bool rts_of(uint8_t ctrl);

    [[nodiscard]] uint32_t expected_response_id(uint32_t request_id);

    // Next legal CAN FD length that can hold hop_bytes of HOP data.
    [[nodiscard]] uint8_t canfd_wire_bytes(size_t hop_bytes);

    [[nodiscard]] can::Message build_message(uint32_t request_id, uint8_t mi, uint8_t si, uint8_t edl,
                                             const std::vector<uint8_t> &payload, bool rts);

    // Parses one received frame. Payload is truncated to EDL; CAN FD padding
    // past 3 + EDL is ignored.
    [[nodiscard]] HopFrame parse_frame(const uint8_t *data, size_t len);

    [[nodiscard]] bool is_error_response(const HopFrame &frame, uint8_t request_mi);

    [[nodiscard]] uint8_t error_code_of(const HopFrame &frame);

    [[nodiscard]] std::string_view error_message(uint8_t error_code);

    void require_positive_timeout(std::chrono::milliseconds timeout_ms);

    [[nodiscard]] std::array<uint8_t, kSlotCount> as_u8_slots(const std::vector<uint8_t> &payload);

    [[nodiscard]] std::array<uint16_t, kSlotCount> as_u16_le_slots(const std::vector<uint8_t> &payload);

    [[nodiscard]] std::string decode_ascii(const std::vector<uint8_t> &payload);

    [[nodiscard]] uint16_t read_u16_le(const uint8_t *data);
    [[nodiscard]] uint32_t read_u32_le(const uint8_t *data);

    void append_u16_le(std::vector<uint8_t> &out, uint16_t value);
    void append_u32_le(std::vector<uint8_t> &out, uint32_t value);

    [[nodiscard]] std::string hex_byte(uint8_t value);

} // namespace linkerbot::hand::o30::protocol

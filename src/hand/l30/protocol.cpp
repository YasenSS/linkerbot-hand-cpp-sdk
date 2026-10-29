#include "linkerbot/hand/l30/protocol.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/errors.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace linkerbot::hand::l30::protocol {

    namespace {
        inline constexpr uint32_t kExtendedIdMask = 0x1FFFFFFFU;
        inline constexpr uint32_t kReservedMask = 0x7U;

        inline constexpr int kPriorityShift = 26;
        inline constexpr int kAccessShift = 25;
        inline constexpr int kParentShift = 21;
        inline constexpr int kSubcmdShift = 13;
        inline constexpr int kDstShift = 8;
        inline constexpr int kSrcShift = 3;

        std::string hex_byte(uint8_t value) {
            static constexpr std::array<char, 16> kDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                                          '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
            std::string out = "0x";
            out += kDigits.at((value >> 4U) & 0x0FU);
            out += kDigits.at(value & 0x0FU);
            return out;
        }

        // Every decoder shares this: BYTE2 must be status OK, BYTE0 must declare
        // exactly the length the caller expects, BYTE1 must be the single-frame
        // marker, and the frame must actually carry the declared bytes.
        //
        // Status is checked before length on purpose. An error ACK carries no
        // payload, so its BYTE0 is 0; checking length first would report a
        // length mismatch and throw away the device's actual reason for
        // refusing. (The Python SDK checks length first and loses the status
        // code on every failed read for exactly this reason.)
        const uint8_t *validate_response_header(const uint8_t *data, size_t len, size_t expected_length) {
            if (data == nullptr || len <= kStatusByteIndex) {
                throw ProtocolError("response too short for status byte");
            }
            const auto status = data[kStatusByteIndex];
            if (status != kStatusOk) {
                throw ProtocolError("L30 status " + hex_byte(status) + ": " + std::string(status_message(status)),
                                    status);
            }
            if (data[kLengthByteIndex] != expected_length) {
                throw ProtocolError("response length mismatch: expected " + std::to_string(expected_length) +
                                    " bytes, got " + std::to_string(static_cast<unsigned>(data[kLengthByteIndex])));
            }
            if (data[kTransactionByteIndex] != kSingleFrameTransaction) {
                throw ProtocolError("unsupported transaction control " + hex_byte(data[kTransactionByteIndex]));
            }
            if (len < kResponseDataOffset + expected_length) {
                throw ProtocolError("response payload too short: expected " + std::to_string(expected_length) +
                                    " bytes");
            }
            return data + kResponseDataOffset;
        }

        int16_t read_i16_be(const uint8_t *data) {
            const auto raw =
                static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) | static_cast<uint16_t>(data[1]));
            return static_cast<int16_t>(raw);
        }

        void append_u16_be(std::vector<uint8_t> &out, uint16_t value) {
            out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
            out.push_back(static_cast<uint8_t>(value & 0xFFU));
        }

        void append_u32_be(std::vector<uint8_t> &out, uint32_t value) {
            out.push_back(static_cast<uint8_t>((value >> 24U) & 0xFFU));
            out.push_back(static_cast<uint8_t>((value >> 16U) & 0xFFU));
            out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
            out.push_back(static_cast<uint8_t>(value & 0xFFU));
        }

        const uint8_t *validate_tactile_frame(const std::vector<uint8_t> &frame, uint8_t transaction,
                                              size_t expected_length) {
            if (frame.size() <= kStatusByteIndex) {
                throw ProtocolError("tactile frame too short for status byte");
            }
            // Status first, for the same reason as validate_response_header: a
            // refused tactile read answers with an ordinary single-frame error
            // ACK, so its BYTE1 is 0x00 and its BYTE0 is 0. Checking the
            // transaction marker first would report a mismatch and bury the
            // reason the device gave.
            const auto status = frame[kStatusByteIndex];
            if (status != kStatusOk) {
                throw ProtocolError(
                    "L30 tactile status " + hex_byte(status) + ": " + std::string(status_message(status)), status);
            }
            if (frame[kTransactionByteIndex] != transaction) {
                throw ProtocolError("tactile transaction mismatch: expected " + hex_byte(transaction) + ", got " +
                                    hex_byte(frame[kTransactionByteIndex]));
            }
            if (frame[kLengthByteIndex] != expected_length) {
                throw ProtocolError("tactile length mismatch: expected " + std::to_string(expected_length) +
                                    " bytes, got " + std::to_string(static_cast<unsigned>(frame[kLengthByteIndex])));
            }
            if (frame.size() < kResponseDataOffset + expected_length) {
                throw ProtocolError("tactile frame payload too short: expected " + std::to_string(expected_length) +
                                    " bytes");
            }
            return frame.data() + kResponseDataOffset;
        }
    } // namespace

    uint32_t FrameId::to_int() const { return build_can_id(*this); }

    FrameId FrameId::response() const {
        FrameId reply = *this;
        reply.dst_id = src_id;
        reply.src_id = dst_id;
        return reply;
    }

    uint32_t build_can_id(const FrameId &fields) {
        if (fields.priority > 7U) {
            throw ValidationError("priority must be between 0 and 7");
        }
        if (fields.access > 1U) {
            throw ValidationError("access must be 0 or 1");
        }
        if (fields.parent > 0x0FU) {
            throw ValidationError("parent command must be between 0x0 and 0xF");
        }
        if (fields.dst_id > kHostIdMax || fields.src_id > kHostIdMax) {
            throw ValidationError("node IDs must be between 0 and 31");
        }
        return (static_cast<uint32_t>(fields.priority) << kPriorityShift) |
               (static_cast<uint32_t>(fields.access) << kAccessShift) |
               (static_cast<uint32_t>(fields.parent) << kParentShift) |
               (static_cast<uint32_t>(fields.subcmd) << kSubcmdShift) |
               (static_cast<uint32_t>(fields.dst_id) << kDstShift) |
               (static_cast<uint32_t>(fields.src_id) << kSrcShift);
    }

    FrameId parse_can_id(uint32_t arbitration_id) {
        if ((arbitration_id & ~kExtendedIdMask) != 0U) {
            throw ValidationError("arbitration ID exceeds 29 bits");
        }
        if ((arbitration_id & kReservedMask) != 0U) {
            throw ValidationError("arbitration ID uses reserved low bits");
        }
        FrameId fields{};
        fields.priority = static_cast<uint8_t>((arbitration_id >> kPriorityShift) & 0x7U);
        fields.access = static_cast<uint8_t>((arbitration_id >> kAccessShift) & 0x1U);
        fields.parent = static_cast<uint8_t>((arbitration_id >> kParentShift) & 0xFU);
        fields.subcmd = static_cast<uint8_t>((arbitration_id >> kSubcmdShift) & 0xFFU);
        fields.dst_id = static_cast<uint8_t>((arbitration_id >> kDstShift) & 0x1FU);
        fields.src_id = static_cast<uint8_t>((arbitration_id >> kSrcShift) & 0x1FU);
        return fields;
    }

    uint32_t expected_response_id(uint32_t request_id) { return build_can_id(parse_can_id(request_id).response()); }

    can::Message build_message(const FrameId &fields, const std::vector<uint8_t> &payload, uint8_t wire_bytes) {
        if (payload.size() > wire_bytes) {
            throw ValidationError("payload of " + std::to_string(payload.size()) +
                                  " bytes exceeds the declared wire length of " +
                                  std::to_string(static_cast<unsigned>(wire_bytes)));
        }
        if (wire_bytes > can::kMaxDataLen) {
            throw ValidationError("wire length exceeds the CAN FD maximum");
        }
        can::Message msg{};
        msg.id = build_can_id(fields);
        msg.is_extended = true;
        msg.is_fd = true;
        // The SocketCAN backend copies dlc straight into canfd_frame.len without
        // rounding up to a legal CAN FD length, so the zero padding to the DLC
        // the protocol mandates has to happen right here.
        msg.dlc = wire_bytes;
        for (size_t i = 0; i < payload.size(); ++i) {
            msg.data[i] = payload[i];
        }
        return msg;
    }

    std::vector<uint8_t> empty_request_payload() { return {kEmptyPayloadLength, kSingleFrameTransaction}; }

    std::vector<uint8_t> ack_payload(uint8_t status) { return {kEmptyPayloadLength, kSingleFrameTransaction, status}; }

    std::vector<uint8_t> encode_i16_vector(const std::array<int16_t, kJointCount> &values) {
        std::vector<uint8_t> out;
        out.reserve(2 + kVectorPayloadBytes);
        out.push_back(kVectorPayloadBytes);
        out.push_back(kSingleFrameTransaction);
        for (const auto value : values) {
            append_u16_be(out, static_cast<uint16_t>(value));
        }
        return out;
    }

    std::vector<uint8_t> encode_u16_vector(const std::array<uint16_t, kJointCount> &values) {
        std::vector<uint8_t> out;
        out.reserve(2 + kVectorPayloadBytes);
        out.push_back(kVectorPayloadBytes);
        out.push_back(kSingleFrameTransaction);
        for (const auto value : values) {
            append_u16_be(out, value);
        }
        return out;
    }

    std::vector<uint8_t> encode_periodic_config(bool enabled, uint32_t period_ms, uint32_t joint_mask) {
        if (enabled) {
            if (period_ms < kPeriodicMinPeriodMs || period_ms > kPeriodicMaxPeriodMs) {
                throw ValidationError("period_ms must be between " + std::to_string(kPeriodicMinPeriodMs) + " and " +
                                      std::to_string(kPeriodicMaxPeriodMs) + ", got " + std::to_string(period_ms));
            }
            if (joint_mask > kJointMaskLimit) {
                throw ValidationError("joint_mask must fit in " + std::to_string(kJointCount) + " bits, got " +
                                      std::to_string(joint_mask));
            }
        } else {
            // Disabling ignores the period and mask entirely; send the documented
            // all-zero form so the device sees an unambiguous "off".
            period_ms = 0;
            joint_mask = kPeriodicAllJointsMask;
        }
        std::vector<uint8_t> out;
        out.reserve(3 + 8);
        out.push_back(kPeriodicConfigPayloadBytes);
        out.push_back(kSingleFrameTransaction);
        out.push_back(enabled ? 0x01U : 0x00U);
        append_u32_be(out, period_ms);
        append_u32_be(out, joint_mask);
        return out;
    }

    std::array<int16_t, kJointCount> decode_i16_response(const uint8_t *data, size_t len) {
        const auto *payload = validate_response_header(data, len, kVectorPayloadBytes);
        std::array<int16_t, kJointCount> values{};
        for (size_t i = 0; i < kJointCount; ++i) {
            values[i] = read_i16_be(payload + (i * 2));
        }
        return values;
    }

    std::array<uint8_t, kJointCount> decode_u8_response(const uint8_t *data, size_t len) {
        const auto *payload = validate_response_header(data, len, kU8VectorPayloadBytes);
        std::array<uint8_t, kJointCount> values{};
        for (size_t i = 0; i < kJointCount; ++i) {
            values[i] = payload[i];
        }
        return values;
    }

    void check_response_status(const uint8_t *data, size_t len) { validate_response_header(data, len, 0); }

    std::vector<uint8_t> decode_response_payload(const uint8_t *data, size_t len, size_t expected_length) {
        const auto *payload = validate_response_header(data, len, expected_length);
        return {payload, payload + expected_length};
    }

    std::string decode_ascii_response(const uint8_t *data, size_t len) {
        if (data == nullptr || len <= kStatusByteIndex) {
            throw ProtocolError("response too short for status byte");
        }
        // ASCII payloads are variable length, so BYTE0 is the only source for the
        // length; validate against what it declares.
        const auto declared = static_cast<size_t>(data[kLengthByteIndex]);
        const auto *payload = validate_response_header(data, len, declared);
        std::string text(reinterpret_cast<const char *>(payload), declared);
        while (!text.empty() && text.back() == '\0') {
            text.pop_back();
        }
        return text;
    }

    std::vector<int16_t> decode_periodic_i16_report(const uint8_t *data, size_t len, uint32_t joint_mask) {
        const auto count = reported_joint_count(joint_mask);
        // Unsolicited reports carry no status byte, so useful data starts at
        // BYTE2 and BYTE0 counts from there.
        const auto expected = count * 2;
        if (data == nullptr || len < kStatusByteIndex) {
            throw ProtocolError("periodic report too short for header");
        }
        if (data[kTransactionByteIndex] != kSingleFrameTransaction) {
            throw ProtocolError("unsupported periodic transaction marker");
        }
        if (data[kLengthByteIndex] != expected) {
            throw ProtocolError("periodic report length mismatch: expected " + std::to_string(expected) +
                                " bytes, got " + std::to_string(static_cast<unsigned>(data[kLengthByteIndex])));
        }
        if (len < kStatusByteIndex + expected) {
            throw ProtocolError("periodic report payload too short: expected " + std::to_string(expected) + " bytes");
        }
        const auto *payload = data + kStatusByteIndex;
        std::vector<int16_t> values;
        values.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            values.push_back(read_i16_be(payload + (i * 2)));
        }
        return values;
    }

    std::vector<uint8_t> decode_periodic_u8_report(const uint8_t *data, size_t len, uint32_t joint_mask) {
        const auto count = reported_joint_count(joint_mask);
        if (data == nullptr || len < kStatusByteIndex) {
            throw ProtocolError("periodic report too short for header");
        }
        if (data[kTransactionByteIndex] != kSingleFrameTransaction) {
            throw ProtocolError("unsupported periodic transaction marker");
        }
        if (data[kLengthByteIndex] != count) {
            throw ProtocolError("periodic report length mismatch: expected " + std::to_string(count) + " bytes, got " +
                                std::to_string(static_cast<unsigned>(data[kLengthByteIndex])));
        }
        if (len < kStatusByteIndex + count) {
            throw ProtocolError("periodic report payload too short: expected " + std::to_string(count) + " bytes");
        }
        const auto *payload = data + kStatusByteIndex;
        return {payload, payload + count};
    }

    std::array<uint8_t, kTactileDataLength> assemble_tactile_payload(const std::vector<uint8_t> &first,
                                                                     const std::vector<uint8_t> &second) {
        const auto *head = validate_tactile_frame(first, kTactileFirstTransaction, kTactileFirstLength);
        const auto *tail = validate_tactile_frame(second, kTactileSecondTransaction, kTactileSecondLength);
        std::array<uint8_t, kTactileDataLength> matrix{};
        for (size_t i = 0; i < kTactileFirstLength; ++i) {
            matrix[i] = head[i];
        }
        for (size_t i = 0; i < kTactileSecondLength; ++i) {
            matrix[kTactileFirstLength + i] = tail[i];
        }
        return matrix;
    }

    std::string_view status_message(uint8_t status) {
        switch (status) {
        case 0x00:
            return "success";
        case 0x10:
            return "DLC or data length mismatch";
        case 0x11:
            return "parameter out of range or invalid";
        case 0x12:
            return "unsupported command code";
        case 0x13:
            return "invalid subcommand";
        case 0x14:
            return "data format error";
        case 0x20:
            return "insufficient permission, not unlocked, or wrong password";
        case 0x21:
            return "zero point not calibrated";
        case 0x22:
            return "motor not enabled";
        case 0x23:
            return "operation not allowed in the current state";
        case 0x24:
            return "not configured (hand type or zero point uninitialized)";
        case 0x30:
            return "motor data read error";
        case 0x31:
            return "motor data write error";
        case 0x32:
            return "multi-frame transfer timeout";
        case 0x33:
            return "incomplete multi-frame data";
        case 0x34:
            return "periodic report configuration failed";
        case 0x35:
            return "reserved";
        case 0x40:
            return "communication timeout";
        case 0x41:
            return "communication lost";
        case 0xF0:
            return "system busy";
        default:
            return "unknown status";
        }
    }

    size_t reported_joint_count(uint32_t joint_mask) {
        if (joint_mask == kPeriodicAllJointsMask) {
            return kJointCount;
        }
        size_t count = 0;
        for (size_t bit = 0; bit < kJointCount; ++bit) {
            if ((joint_mask & (1U << bit)) != 0U) {
                ++count;
            }
        }
        return count;
    }

} // namespace linkerbot::hand::l30::protocol

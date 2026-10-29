#include "linkerbot/hand/o30/protocol.hpp"
#include <linkerbot/hand/errors.hpp>

#include <linkerbot/hand/protocol/hop.hpp>
#include <stdexcept>

namespace linkerbot::hand::o30::protocol {

    uint8_t ctrl_byte(uint8_t mi, bool rts) { return hand::protocol::hop::ctrl_byte(mi, rts); }

    uint8_t mi_of(uint8_t ctrl) { return hand::protocol::hop::mi_of(ctrl); }

    bool rts_of(uint8_t ctrl) { return hand::protocol::hop::rts_of(ctrl); }

    uint32_t expected_response_id(uint32_t request_id) { return hand::protocol::hop::expected_response_id(request_id); }

    uint8_t canfd_wire_bytes(size_t hop_bytes) { return hand::protocol::hop::canfd_wire_bytes(hop_bytes); }

    can::Message build_message(uint32_t request_id, uint8_t mi, uint8_t si, uint8_t edl,
                               const std::vector<uint8_t> &payload, bool rts) {
        return hand::protocol::hop::build_message(request_id, mi, si, edl, payload, rts);
    }

    HopFrame parse_frame(const uint8_t *data, size_t len) {
        try {
            const auto value = hand::protocol::hop::parse_frame(data, len);
            return {value.mi, value.rts, value.si, value.edl, value.payload};
        } catch (const hand::ProtocolError &error) {
            throw ProtocolError(error.what());
        }
    }

    bool is_error_response(const HopFrame &frame, uint8_t request_mi) {
        // A solicited 0x4F read returns a 4F header as the object itself.
        return request_mi != kMiCommError && frame.mi == kMiCommError && frame.edl == 1 && frame.payload.size() == 1;
    }

    uint8_t error_code_of(const HopFrame &frame) {
        if (frame.payload.empty()) {
            return 0;
        }
        return frame.payload[0];
    }

    std::string_view error_message(uint8_t error_code) {
        switch (error_code) {
        case kErrorSubIndexMissing:
            return "sub-index missing or out of range";
        case kErrorIllegalValue:
            return "illegal data value";
        default:
            return "unknown HOP error";
        }
    }

    void require_positive_timeout(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
    }

    std::array<uint8_t, kSlotCount> as_u8_slots(const std::vector<uint8_t> &payload) {
        if (payload.size() != kSlotCount) {
            throw ProtocolError("expected a 36-byte HOP object, got " + std::to_string(payload.size()) + " bytes");
        }
        std::array<uint8_t, kSlotCount> slots{};
        for (size_t i = 0; i < kSlotCount; ++i) {
            slots[i] = payload[i];
        }
        return slots;
    }

    std::array<uint16_t, kSlotCount> as_u16_le_slots(const std::vector<uint8_t> &payload) {
        if (payload.size() != kU16ObjectBytes) {
            throw ProtocolError("expected a 72-byte half-word object, got " + std::to_string(payload.size()) +
                                " bytes");
        }
        std::array<uint16_t, kSlotCount> slots{};
        for (size_t i = 0; i < kSlotCount; ++i) {
            slots[i] = read_u16_le(payload.data() + (i * 2));
        }
        return slots;
    }

    std::string decode_ascii(const std::vector<uint8_t> &payload) {
        std::string text;
        for (const auto byte : payload) {
            if (byte == 0) {
                break;
            }
            if (byte > 0x7F) {
                throw ProtocolError("HOP text is not ASCII");
            }
            text.push_back(static_cast<char>(byte));
        }
        const auto first = text.find_first_not_of(" \t\r\n\v\f");
        if (first == std::string::npos) {
            return {};
        }
        return text.substr(first, text.find_last_not_of(" \t\r\n\v\f") - first + 1);
    }

    uint16_t read_u16_le(const uint8_t *data) { return hand::protocol::hop::read_u16_le(data); }

    uint32_t read_u32_le(const uint8_t *data) { return hand::protocol::hop::read_u32_le(data); }

    void append_u16_le(std::vector<uint8_t> &out, uint16_t value) { hand::protocol::hop::append_u16_le(out, value); }

    void append_u32_le(std::vector<uint8_t> &out, uint32_t value) { hand::protocol::hop::append_u32_le(out, value); }

    std::string hex_byte(uint8_t value) {
        static constexpr std::array<char, 16> kDigits{'0', '1', '2', '3', '4', '5', '6', '7',
                                                      '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
        std::string out = "0x";
        out += kDigits.at((value >> 4U) & 0x0FU);
        out += kDigits.at(value & 0x0FU);
        return out;
    }

} // namespace linkerbot::hand::o30::protocol

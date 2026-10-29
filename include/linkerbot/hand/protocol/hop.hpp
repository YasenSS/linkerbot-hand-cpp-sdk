#pragma once
// Shared value-only HOP framing. Model-specific registers and response policies
// deliberately remain in each model's protocol backend.
#include <algorithm>
#include <array>
#include <cstdint>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/errors.hpp>
#include <string>
#include <vector>

namespace linkerbot::hand::protocol::hop {
    inline constexpr size_t kHeaderBytes = 3, kMaxHopPayload = 61, kMaxHopFrame = 64;
    inline constexpr uint8_t kCtrlMiMask = 0x7f, kCtrlRtsBit = 0x80;
    inline constexpr uint32_t kResponseIdOffset = 0x400;
    struct HopFrame {
        uint8_t mi = 0;
        bool rts = false;
        uint8_t si = 0, edl = 0;
        std::vector<uint8_t> payload;
    };
    inline uint8_t ctrl_byte(uint8_t mi, bool rts) {
        if ((mi & kCtrlRtsBit) != 0U) {
            throw ValidationError("MI must fit in 7 bits");
        }
        return static_cast<uint8_t>(mi | (rts ? kCtrlRtsBit : 0U));
    }

    inline uint8_t mi_of(uint8_t ctrl) { return static_cast<uint8_t>(ctrl & kCtrlMiMask); }

    inline bool rts_of(uint8_t ctrl) { return (ctrl & kCtrlRtsBit) != 0U; }

    inline uint32_t expected_response_id(uint32_t request_id) {
        if (request_id > 0x3FF) {
            throw ValidationError("HOP request ID must be within 0..0x3FF");
        }
        return request_id + kResponseIdOffset;
    }

    inline uint8_t canfd_wire_bytes(size_t hop_bytes) {
        if (hop_bytes > can::kMaxDataLen) {
            throw ValidationError("HOP frame exceeds the CAN FD maximum");
        }
        for (const auto length : std::array<uint8_t, 16>{0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64}) {
            if (length >= hop_bytes) {
                return length;
            }
        }
        throw ValidationError("HOP frame exceeds the CAN FD maximum");
    }

    inline can::Message build_message(uint32_t request_id, uint8_t mi, uint8_t si, uint8_t edl,
                                      const std::vector<uint8_t> &payload, bool rts) {
        static_cast<void>(expected_response_id(request_id));
        if (edl == 0 || static_cast<size_t>(si) + edl > 256) {
            throw ValidationError("HOP SI/EDL must address 1..255 bytes within one 256-byte object");
        }
        if (payload.size() > kMaxHopPayload) {
            throw ValidationError("write payload of " + std::to_string(payload.size()) +
                                  " bytes exceeds the single-frame HOP maximum of " + std::to_string(kMaxHopPayload));
        }
        if (!payload.empty() && payload.size() != edl) {
            throw ValidationError("write EDL " + std::to_string(static_cast<unsigned>(edl)) +
                                  " does not match payload size " + std::to_string(payload.size()));
        }

        const auto hop_bytes = kHeaderBytes + payload.size();
        can::Message msg{};
        msg.id = request_id;
        msg.is_extended = false;
        msg.is_fd = true;
        msg.is_brs = false;
        msg.dlc = canfd_wire_bytes(hop_bytes);
        msg.data[0] = ctrl_byte(mi, rts);
        msg.data[1] = si;
        msg.data[2] = edl;
        for (size_t i = 0; i < payload.size(); ++i) {
            msg.data[kHeaderBytes + i] = payload[i];
        }
        return msg;
    }

    inline HopFrame parse_frame(const uint8_t *data, size_t len) {
        if (data == nullptr || len < kHeaderBytes || len > kMaxHopFrame) {
            throw ProtocolError("HOP frame too short for CTRL/SI/EDL");
        }
        HopFrame frame{};
        frame.mi = mi_of(data[0]);
        frame.rts = rts_of(data[0]);
        frame.si = data[1];
        frame.edl = data[2];
        if (frame.edl == 0 || frame.edl > kMaxHopPayload || static_cast<size_t>(frame.si) + frame.edl > 256) {
            throw ProtocolError("invalid HOP fragment SI/EDL");
        }
        if (len < kHeaderBytes + frame.edl) {
            throw ProtocolError(
                "HOP payload shorter than EDL: EDL=" + std::to_string(static_cast<unsigned>(frame.edl)) +
                ", available=" + std::to_string(len - kHeaderBytes));
        }
        frame.payload.assign(data + kHeaderBytes, data + kHeaderBytes + frame.edl);
        return frame;
    }

    inline uint16_t read_u16_le(const uint8_t *data) {
        return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8U));
    }

    inline uint32_t read_u32_le(const uint8_t *data) {
        return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8U) |
               (static_cast<uint32_t>(data[2]) << 16U) | (static_cast<uint32_t>(data[3]) << 24U);
    }

    inline void append_u16_le(std::vector<uint8_t> &out, uint16_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFFU));
        out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
    }

    inline void append_u32_le(std::vector<uint8_t> &out, uint32_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFFU));
        out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
        out.push_back(static_cast<uint8_t>((value >> 16U) & 0xFFU));
        out.push_back(static_cast<uint8_t>((value >> 24U) & 0xFFU));
    }
}

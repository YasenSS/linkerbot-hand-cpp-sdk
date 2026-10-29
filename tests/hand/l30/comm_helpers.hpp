#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

// Shared FakeTransport plumbing for the L30 offline tests.
//
// L30 differs from the classic-CAN hands in two ways that matter here:
//
//  1. FakeTransport indexes handlers by `message.id`, and every L30 subsystem
//     uses a different 29-bit arbitration ID. Where L20lite registers one
//     handler on 0x28 and serves everything, L30 needs one handler per
//     (parent, subcmd, access) triple -- hence `request_id()` and the
//     `install_*` helpers below.
//  2. Client::on_message drops any frame that is not extended, and filters on
//     DstID/SrcID. Every reply must therefore carry is_extended / is_fd and be
//     addressed back to the host, which `fd_reply()` guarantees.
namespace l30_comm {

    using linkerbot::can::FakeTransport;
    using linkerbot::can::Message;

    namespace protocol = linkerbot::hand::l30::protocol;

    constexpr uint8_t kNodeId = 1;
    constexpr uint8_t kHostId = 0;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    // ---- Arbitration IDs ---------------------------------------------------

    inline uint32_t request_id(uint8_t parent, uint8_t subcmd, uint8_t access, uint8_t node_id = kNodeId,
                               uint8_t host_id = kHostId) {
        protocol::FrameId fields{};
        fields.access = access;
        fields.parent = parent;
        fields.subcmd = subcmd;
        fields.dst_id = node_id;
        fields.src_id = host_id;
        return fields.to_int();
    }

    inline uint32_t response_id(uint8_t parent, uint8_t subcmd, uint8_t access, uint8_t node_id = kNodeId,
                                uint8_t host_id = kHostId) {
        return protocol::expected_response_id(request_id(parent, subcmd, access, node_id, host_id));
    }

    inline uint32_t write_id(uint8_t subcmd, uint8_t node_id = kNodeId) {
        return request_id(protocol::kParentControl, subcmd, protocol::kAccessWrite, node_id);
    }

    inline uint32_t query_id(uint8_t subcmd, uint8_t node_id = kNodeId) {
        return request_id(protocol::kParentQuery, subcmd, protocol::kAccessRead, node_id);
    }

    inline uint32_t report_id(uint8_t subcmd, uint8_t node_id = kNodeId, uint8_t host_id = kHostId) {
        // A periodic report travels device -> host, so DstID is the host and
        // SrcID the node. Access stays 0: handle_report rejects Access=1.
        protocol::FrameId fields{};
        fields.access = protocol::kAccessRead;
        fields.parent = protocol::kParentPeriodic;
        fields.subcmd = subcmd;
        fields.dst_id = host_id;
        fields.src_id = node_id;
        return fields.to_int();
    }

    // ---- Frame construction ------------------------------------------------

    inline Message fd_reply(uint32_t id, const std::vector<uint8_t> &payload) {
        Message reply;
        reply.id = id;
        reply.is_extended = true;
        reply.is_fd = true;
        reply.dlc = static_cast<uint8_t>(payload.size());
        for (size_t i = 0; i < payload.size(); ++i) {
            reply.data[i] = payload[i];
        }
        return reply;
    }

    // ---- Payload builders --------------------------------------------------

    // [len, txn, status, <big-endian i16 x 17>]
    inline std::vector<uint8_t> i16_response(const std::array<int16_t, linkerbot::hand::l30::kJointCount> &values,
                                             uint8_t status = protocol::kStatusOk) {
        std::vector<uint8_t> out{protocol::kVectorPayloadBytes, protocol::kSingleFrameTransaction, status};
        for (const auto value : values) {
            const auto raw = static_cast<uint16_t>(value);
            out.push_back(static_cast<uint8_t>((raw >> 8U) & 0xFFU));
            out.push_back(static_cast<uint8_t>(raw & 0xFFU));
        }
        return out;
    }

    inline std::vector<uint8_t> u8_response(const std::array<uint8_t, linkerbot::hand::l30::kJointCount> &values,
                                            uint8_t status = protocol::kStatusOk) {
        std::vector<uint8_t> out{protocol::kU8VectorPayloadBytes, protocol::kSingleFrameTransaction, status};
        for (const auto value : values) {
            out.push_back(value);
        }
        return out;
    }

    // Declared length comes from `bytes` itself, which is what the ASCII and
    // DeviceInfo decoders read.
    inline std::vector<uint8_t> payload_response(const std::vector<uint8_t> &bytes,
                                                 uint8_t status = protocol::kStatusOk) {
        std::vector<uint8_t> out{static_cast<uint8_t>(bytes.size()), protocol::kSingleFrameTransaction, status};
        out.insert(out.end(), bytes.begin(), bytes.end());
        return out;
    }

    // Periodic reports are unsolicited and carry NO status byte: BYTE0 is the
    // length, BYTE1 the transaction marker, and data starts at BYTE2.
    inline std::vector<uint8_t>
    periodic_i16_payload(const std::array<int16_t, linkerbot::hand::l30::kJointCount> &values) {
        std::vector<uint8_t> out{protocol::kVectorPayloadBytes, protocol::kSingleFrameTransaction};
        for (const auto value : values) {
            const auto raw = static_cast<uint16_t>(value);
            out.push_back(static_cast<uint8_t>((raw >> 8U) & 0xFFU));
            out.push_back(static_cast<uint8_t>(raw & 0xFFU));
        }
        return out;
    }

    inline std::vector<uint8_t>
    periodic_u8_payload(const std::array<uint8_t, linkerbot::hand::l30::kJointCount> &values) {
        std::vector<uint8_t> out{protocol::kU8VectorPayloadBytes, protocol::kSingleFrameTransaction};
        for (const auto value : values) {
            out.push_back(value);
        }
        return out;
    }

    inline Message periodic_i16_frame(uint8_t subcmd,
                                      const std::array<int16_t, linkerbot::hand::l30::kJointCount> &values,
                                      uint8_t node_id = kNodeId) {
        return fd_reply(report_id(subcmd, node_id), periodic_i16_payload(values));
    }

    inline Message periodic_u8_frame(uint8_t subcmd,
                                     const std::array<uint8_t, linkerbot::hand::l30::kJointCount> &values,
                                     uint8_t node_id = kNodeId) {
        return fd_reply(report_id(subcmd, node_id), periodic_u8_payload(values));
    }

    inline std::array<int16_t, linkerbot::hand::l30::kJointCount> filled_i16(int16_t value) {
        std::array<int16_t, linkerbot::hand::l30::kJointCount> values{};
        values.fill(value);
        return values;
    }

    inline std::array<uint8_t, linkerbot::hand::l30::kJointCount> filled_u8(uint8_t value) {
        std::array<uint8_t, linkerbot::hand::l30::kJointCount> values{};
        values.fill(value);
        return values;
    }

    // ---- Handler factories -------------------------------------------------
    //
    // Every factory derives the reply ID from the request via
    // expected_response_id(), so one factory serves any subcommand.

    inline FakeTransport::Handler ack_handler(uint8_t status = protocol::kStatusOk) {
        return [status](const Message &req) {
            return std::vector<Message>{
                fd_reply(protocol::expected_response_id(req.id), protocol::ack_payload(status))};
        };
    }

    inline FakeTransport::Handler i16_handler(const std::array<int16_t, linkerbot::hand::l30::kJointCount> &values,
                                              uint8_t status = protocol::kStatusOk) {
        const auto payload = i16_response(values, status);
        return [payload](const Message &req) {
            return std::vector<Message>{fd_reply(protocol::expected_response_id(req.id), payload)};
        };
    }

    inline FakeTransport::Handler u8_handler(const std::array<uint8_t, linkerbot::hand::l30::kJointCount> &values,
                                             uint8_t status = protocol::kStatusOk) {
        const auto payload = u8_response(values, status);
        return [payload](const Message &req) {
            return std::vector<Message>{fd_reply(protocol::expected_response_id(req.id), payload)};
        };
    }

    inline FakeTransport::Handler payload_handler(const std::vector<uint8_t> &bytes,
                                                  uint8_t status = protocol::kStatusOk) {
        const auto payload = payload_response(bytes, status);
        return [payload](const Message &req) {
            return std::vector<Message>{fd_reply(protocol::expected_response_id(req.id), payload)};
        };
    }

    // Raw payload, no framing added -- for malformed-response cases.
    inline FakeTransport::Handler raw_handler(const std::vector<uint8_t> &payload) {
        return [payload](const Message &req) {
            return std::vector<Message>{fd_reply(protocol::expected_response_id(req.id), payload)};
        };
    }

    // One tactile read answers with TWO frames: 0x10 carrying 61 bytes and
    // 0x11 carrying the remaining 11.
    inline std::vector<Message> tactile_frames(const std::array<uint8_t, protocol::kTactileDataLength> &matrix,
                                               uint32_t reply_id, uint8_t status = protocol::kStatusOk,
                                               uint8_t first_txn = protocol::kTactileFirstTransaction,
                                               uint8_t second_txn = protocol::kTactileSecondTransaction) {
        std::vector<uint8_t> first{static_cast<uint8_t>(protocol::kTactileFirstLength), first_txn, status};
        for (size_t i = 0; i < protocol::kTactileFirstLength; ++i) {
            first.push_back(matrix[i]);
        }
        std::vector<uint8_t> second{static_cast<uint8_t>(protocol::kTactileSecondLength), second_txn, status};
        for (size_t i = 0; i < protocol::kTactileSecondLength; ++i) {
            second.push_back(matrix[protocol::kTactileFirstLength + i]);
        }
        return {fd_reply(reply_id, first), fd_reply(reply_id, second)};
    }

    inline FakeTransport::Handler tactile_handler(const std::array<uint8_t, protocol::kTactileDataLength> &matrix,
                                                  uint8_t status = protocol::kStatusOk,
                                                  uint8_t first_txn = protocol::kTactileFirstTransaction,
                                                  uint8_t second_txn = protocol::kTactileSecondTransaction) {
        return [matrix, status, first_txn, second_txn](const Message &req) {
            return tactile_frames(matrix, protocol::expected_response_id(req.id), status, first_txn, second_txn);
        };
    }

    inline std::array<uint8_t, protocol::kTactileDataLength> tactile_ramp() {
        std::array<uint8_t, protocol::kTactileDataLength> matrix{};
        for (size_t i = 0; i < matrix.size(); ++i) {
            matrix[i] = static_cast<uint8_t>(i);
        }
        return matrix;
    }

    // ---- Bulk registration -------------------------------------------------

    // Every periodic-report config ID answers with an OK ACK. L30's default
    // constructor enables angle reporting and BLOCKS on that ACK, so a test
    // that lets auto_start_periodic default to true must install these first.
    inline void install_periodic_acks(FakeTransport *transport, uint8_t node_id = kNodeId) {
        for (const auto subcmd : {protocol::kSubPosition, protocol::kSubSpeed, protocol::kSubCurrent,
                                  protocol::kSubTemperature, protocol::kSubFault}) {
            transport->on_request(request_id(protocol::kParentPeriodic, subcmd, protocol::kAccessWrite, node_id),
                                  ack_handler());
        }
    }

    // ---- Inspection --------------------------------------------------------

    inline std::vector<uint8_t> last_payload(FakeTransport *transport) {
        const auto sent = transport->sent();
        if (sent.empty()) {
            return {};
        }
        const auto &msg = sent.back();
        return {msg.data.begin(), msg.data.begin() + msg.dlc};
    }

    inline std::vector<std::vector<uint8_t>> payloads(FakeTransport *transport) {
        std::vector<std::vector<uint8_t>> out;
        for (const auto &msg : transport->sent()) {
            out.emplace_back(msg.data.begin(), msg.data.begin() + msg.dlc);
        }
        return out;
    }

    inline std::vector<uint32_t> sent_ids(FakeTransport *transport) {
        std::vector<uint32_t> out;
        for (const auto &msg : transport->sent()) {
            out.push_back(msg.id);
        }
        return out;
    }

    // Frames addressed to `id`, newest last. Useful when a test drives several
    // subsystems and only cares about one of them.
    inline std::vector<Message> frames_for(FakeTransport *transport, uint32_t id) {
        std::vector<Message> out;
        for (const auto &msg : transport->sent()) {
            if (msg.id == id) {
                out.push_back(msg);
            }
        }
        return out;
    }

    inline size_t count_id(FakeTransport *transport, uint32_t id) { return frames_for(transport, id).size(); }

    inline std::vector<uint8_t> trimmed(const Message &msg) { return {msg.data.begin(), msg.data.begin() + msg.dlc}; }

    // Payload bytes that carry meaning, with the CAN FD zero padding dropped.
    // A request declares its useful length in BYTE0.
    inline std::vector<uint8_t> useful(const Message &msg) {
        const auto full = trimmed(msg);
        if (full.size() < 2) {
            return full;
        }
        const auto declared = static_cast<size_t>(full[protocol::kLengthByteIndex]);
        const auto end = std::min(full.size(), declared + 2);
        return {full.begin(), full.begin() + static_cast<std::ptrdiff_t>(end)};
    }

} // namespace l30_comm

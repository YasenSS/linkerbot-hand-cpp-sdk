#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

// Shared FakeTransport plumbing for O30 HOP tests.
//
// Every request uses one standard CAN ID (default 0x001). The fake endpoint
// inspects CTRL/SI/EDL, serves object slices, fragments replies at 61 bytes,
// and records writes. CAN FD padding past 3+EDL is ignored, matching the
// protocol and the Python FakeO30Dispatcher.
namespace o30_comm {

    using linkerbot::can::FakeTransport;
    using linkerbot::can::Message;
    using linkerbot::hand::o30::kJointCount;
    using linkerbot::hand::o30::kSlotCount;
    using linkerbot::hand::o30::O30;

    namespace protocol = linkerbot::hand::o30::protocol;

    constexpr auto kTimeout = std::chrono::milliseconds{500};

    struct RecordedWrite {
        uint8_t mi = 0;
        uint8_t si = 0;
        std::vector<uint8_t> payload;
    };

    inline std::vector<uint8_t> hop_useful(const Message &msg) {
        if (msg.dlc < protocol::kHeaderBytes) {
            return {msg.data.begin(), msg.data.begin() + msg.dlc};
        }
        const auto edl = static_cast<size_t>(msg.data[2]);
        auto end = protocol::kHeaderBytes;
        if (msg.dlc > protocol::kHeaderBytes) {
            end += edl;
        }
        end = std::min(end, static_cast<size_t>(msg.dlc));
        return {msg.data.begin(), msg.data.begin() + static_cast<std::ptrdiff_t>(end)};
    }

    inline Message hop_reply(uint32_t id, uint8_t mi, bool rts, uint8_t si, const std::vector<uint8_t> &payload) {
        // Independent of the SDK request encoder: do not share wire-format bugs.
        if (payload.empty() || payload.size() > 61) {
            throw std::invalid_argument("fake HOP reply requires 1..61 bytes");
        }
        Message msg{};
        msg.id = id;
        msg.is_fd = true;
        msg.data[0] = static_cast<uint8_t>(mi | (rts ? 0x80 : 0));
        msg.data[1] = si;
        msg.data[2] = static_cast<uint8_t>(payload.size());
        constexpr std::array<uint8_t, 16> lengths{0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
        msg.dlc = *std::lower_bound(lengths.begin(), lengths.end(), 3 + payload.size());
        std::copy(payload.begin(), payload.end(), msg.data.begin() + 3);
        return msg;
    }

    inline Message error_reply(uint32_t id, uint8_t si, uint8_t code) {
        return hop_reply(id, protocol::kMiCommError, false, si, {code});
    }

    class HopEndpoint {
    public:
        uint32_t request_id = protocol::kDefaultRequestId;
        std::map<std::pair<uint8_t, bool>, std::vector<uint8_t>> objects;
        std::vector<RecordedWrite> writes;

        [[nodiscard]] uint32_t response_id() const { return protocol::expected_response_id(request_id); }

        void put(uint8_t mi, std::vector<uint8_t> payload, bool rts = false) {
            objects[{mi, rts}] = std::move(payload);
        }

        void put_ascii(uint8_t mi, size_t offset, size_t length, const std::string &text, bool rts = false) {
            auto &object = objects[{mi, rts}];
            const auto need = offset + length;
            if (object.size() < need) {
                object.resize(need, 0);
            }
            for (size_t i = 0; i < length; ++i) {
                object[offset + i] = i < text.size() ? static_cast<uint8_t>(text[i]) : 0;
            }
        }

        void seed_defaults() {
            const std::array<uint8_t, 17> u8_mis{
                protocol::kMiMapping,      protocol::kMiPosition,       protocol::kMiSpeed,
                protocol::kMiAcceleration, protocol::kMiCurrent,        protocol::kMiVoltage,
                protocol::kMiTorque,       protocol::kMiTemperature,    protocol::kMiMotionTime,
                protocol::kMiStallTime,    protocol::kMiStallThreshold, protocol::kMiStallHoldCurrent,
                protocol::kMiValidJoint,   protocol::kMiFault,          protocol::kMiPositionPidP,
                protocol::kMiPositionPidI, protocol::kMiPositionPidD};
            for (const auto mi : u8_mis) {
                put(mi, std::vector<uint8_t>(protocol::kU8ObjectBytes, 0));
            }
            put(protocol::kMiSpeedPidP, std::vector<uint8_t>(protocol::kU8ObjectBytes, 0));
            put(protocol::kMiSpeedPidI, std::vector<uint8_t>(protocol::kU8ObjectBytes, 0));
            put(protocol::kMiSpeedPidD, std::vector<uint8_t>(protocol::kU8ObjectBytes, 0));
            put(protocol::kMiPosition16, std::vector<uint8_t>(protocol::kU16ObjectBytes, 0));
            put(protocol::kMiSensorInfo, std::vector<uint8_t>(50, 0));
            put_ascii(protocol::kMiSensorInfo, 0x00, 0x20, "NO_SENSOR");
            put(protocol::kMiConfig, std::vector<uint8_t>(protocol::kConfigBytes, 0));
            for (uint8_t mi = protocol::kMiPreset1; mi <= protocol::kMiPreset5; ++mi) {
                put(mi, std::vector<uint8_t>(protocol::kPresetBytes, 0));
            }
            put(protocol::kMiProductInfo, std::vector<uint8_t>(protocol::kProductInfoBytes, 0));
            put_ascii(protocol::kMiProductInfo, 0x00, 0x10, "O30");
            put_ascii(protocol::kMiProductInfo, 0x10, 0x10, "12V~24V");
            put_ascii(protocol::kMiProductInfo, 0x20, 0x19, "MCU-UID-123");
            put_ascii(protocol::kMiProductInfo, 0x39, 0x20, "DEVICE-UID-O30");
            put_ascii(protocol::kMiProductInfo, 0x59, 0x08, "HOP");
            put_ascii(protocol::kMiProductInfo, 0x61, 0x08, "0.0.3");
            put_ascii(protocol::kMiProductInfo, 0x69, 0x08, "V1.2.0");
            put_ascii(protocol::kMiProductInfo, 0x71, 0x08, "V1.1.0");
            put_ascii(protocol::kMiProductInfo, 0x79, 0x08, "V0.9.0");
            put_ascii(protocol::kMiProductInfo, 0x81, 0x08, "1.0.0");
            put_ascii(protocol::kMiProductInfo, 0x89, 0x08, "1.1.0");
            put_ascii(protocol::kMiProductInfo, 0x91, 0x08, "M1.0.0");
            put_ascii(protocol::kMiProductInfo, 0x99, 0x20, "2026-07-08 15:33:08");
            put_ascii(protocol::kMiProductInfo, 0xB9, 0x20, "CAN,UART");
            put(protocol::kMiService, std::vector<uint8_t>(256, 0));
            put(protocol::kMiUnit, std::vector<uint8_t>(32, 0));
            put_ascii(protocol::kMiUnit, 0x01, 0x11, "normalization");
            objects[{protocol::kMiUnit, false}][0x12] = 0x00;
            objects[{protocol::kMiUnit, false}][0x13] = 0x00;
            objects[{protocol::kMiUnit, false}][0x14] = 0xFF;
            objects[{protocol::kMiUnit, false}][0x15] = 0x00;
            std::vector<uint8_t> errors(1 + protocol::kErrorHistoryBytes, 0);
            for (uint8_t i = 0; i < protocol::kErrorHistoryBytes; ++i) {
                errors[static_cast<size_t>(i) + 1] = static_cast<uint8_t>(i + 1);
            }
            put(protocol::kMiCommError, std::move(errors));
        }

        std::vector<Message> reply(const Message &req) {
            if (req.is_extended || req.dlc < protocol::kHeaderBytes) {
                return {};
            }
            const auto mi = protocol::mi_of(req.data[0]);
            const auto rts = protocol::rts_of(req.data[0]);
            const auto si = req.data[1];
            const auto edl = req.data[2];
            const bool is_write = req.dlc > protocol::kHeaderBytes;
            if (is_write) {
                if (req.dlc < protocol::kHeaderBytes + edl) {
                    return {};
                }
                std::vector<uint8_t> payload(req.data.begin() + protocol::kHeaderBytes,
                                             req.data.begin() + protocol::kHeaderBytes + edl);
                writes.push_back({mi, si, payload});
                auto &object = objects[{mi, false}];
                const auto end = static_cast<size_t>(si) + payload.size();
                if (object.size() < end) {
                    object.resize(end, 0);
                }
                std::copy(payload.begin(), payload.end(), object.begin() + si);
                return {hop_reply(response_id(), mi, rts, si, payload)};
            }

            auto it = objects.find({mi, rts});
            if (it == objects.end()) {
                it = objects.find({mi, false});
            }
            if (it == objects.end()) {
                return {};
            }
            const auto requested_end = static_cast<size_t>(si) + edl;
            if (requested_end > it->second.size()) {
                return {error_reply(response_id(), si, protocol::kErrorSubIndexMissing)};
            }
            const std::vector<uint8_t> slice(it->second.begin() + si,
                                             it->second.begin() + static_cast<std::ptrdiff_t>(requested_end));
            std::vector<Message> frames;
            for (size_t offset = 0; offset < slice.size(); offset += protocol::kMaxHopPayload) {
                const auto n = std::min(protocol::kMaxHopPayload, slice.size() - offset);
                frames.push_back(hop_reply(response_id(), mi, rts,
                                           static_cast<uint8_t>(static_cast<size_t>(si) + offset),
                                           {slice.begin() + static_cast<std::ptrdiff_t>(offset),
                                            slice.begin() + static_cast<std::ptrdiff_t>(offset + n)}));
            }
            return frames;
        }

        FakeTransport::Handler handler() {
            return [this](const Message &req) { return reply(req); };
        }
    };

    struct Harness {
        FakeTransport *transport = nullptr;
        std::shared_ptr<HopEndpoint> hop;
        std::unique_ptr<O30> hand;
    };

    inline void attach_hop(FakeTransport *transport, const std::shared_ptr<HopEndpoint> &hop) {
        transport->on_request(hop->request_id, hop->handler());
    }

    inline Harness make_hand(uint32_t request_id = protocol::kDefaultRequestId) {
        Harness out;
        out.hop = std::make_shared<HopEndpoint>();
        out.hop->request_id = request_id;
        out.hop->seed_defaults();
        auto transport = std::make_unique<FakeTransport>();
        out.transport = transport.get();
        attach_hop(out.transport, out.hop);
        out.hand = std::make_unique<O30>(std::move(transport), request_id);
        return out;
    }

    inline std::vector<uint8_t> ramp_slots() {
        std::vector<uint8_t> bytes(protocol::kU8ObjectBytes, 0);
        for (size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = static_cast<uint8_t>(i);
        }
        return bytes;
    }

    inline std::array<uint8_t, kJointCount> projected_ramp() {
        std::array<uint8_t, kSlotCount> slots{};
        for (size_t i = 0; i < slots.size(); ++i) {
            slots[i] = static_cast<uint8_t>(i);
        }
        return linkerbot::hand::o30::project_u8(slots);
    }

    inline std::array<uint8_t, kJointCount> filled_u8(uint8_t value) {
        std::array<uint8_t, kJointCount> values{};
        values.fill(value);
        return values;
    }

    inline std::array<float, kJointCount> filled_percent(float value) {
        std::array<float, kJointCount> values{};
        values.fill(value);
        return values;
    }

    inline std::vector<std::vector<uint8_t>> payloads(FakeTransport *transport) {
        std::vector<std::vector<uint8_t>> out;
        for (const auto &msg : transport->sent()) {
            out.push_back(hop_useful(msg));
        }
        return out;
    }

    inline std::vector<uint8_t> last_payload(FakeTransport *transport) {
        const auto all = payloads(transport);
        if (all.empty()) {
            return {};
        }
        return all.back();
    }

    inline std::vector<RecordedWrite> write_segments(const std::shared_ptr<HopEndpoint> &hop, uint8_t mi) {
        std::vector<RecordedWrite> out;
        for (const auto &write : hop->writes) {
            if (write.mi == mi) {
                out.push_back(write);
            }
        }
        return out;
    }

    inline bool contains(const std::string &haystack, const std::string &needle) {
        return haystack.find(needle) != std::string::npos;
    }

} // namespace o30_comm

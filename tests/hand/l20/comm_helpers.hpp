#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>

namespace l20_comm {

    using linkerbot::can::FakeTransport;
    using linkerbot::can::Message;

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint32_t kRightId = 0x27;

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

    // Echo/sense for a five-frame domain whose cmds are first_cmd .. first_cmd+4.
    // Sense (DLC=1) replies cmd + 6 fill bytes; a write echoes the payload.
    inline FakeTransport::Handler five_handler(uint32_t id, uint8_t first_cmd, uint8_t fill) {
        return [id, first_cmd, fill](const Message &req) {
            if (req.dlc < 1) {
                return std::vector<Message>{};
            }
            const uint8_t cmd = req.data[0];
            if (cmd < first_cmd || cmd > static_cast<uint8_t>(first_cmd + 4)) {
                return std::vector<Message>{};
            }
            Message reply;
            reply.id = id;
            reply.dlc = 7;
            reply.data[0] = cmd;
            if (req.dlc > 1) {
                for (size_t i = 1; i < 7; ++i) {
                    reply.data[i] = (i < req.dlc) ? req.data[i] : fill;
                }
            } else {
                for (size_t i = 1; i < 7; ++i) {
                    reply.data[i] = fill;
                }
            }
            return std::vector<Message>{reply};
        };
    }

} // namespace l20_comm

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>

namespace l20lite_comm {

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

    inline Message reply_cmd(uint32_t id, uint8_t cmd, size_t n, uint8_t fill) {
        Message reply;
        reply.id = id;
        reply.dlc = static_cast<uint8_t>(1 + n);
        reply.data[0] = cmd;
        for (size_t i = 0; i < n; ++i) {
            reply.data[i + 1] = fill;
        }
        return reply;
    }

    inline FakeTransport::Handler dual_handler(uint32_t id, uint8_t cmd_a, size_t n_a, uint8_t cmd_b, size_t n_b,
                                               uint8_t fill) {
        return [id, cmd_a, n_a, cmd_b, n_b, fill](const Message &req) {
            if (req.dlc < 1) {
                return std::vector<Message>{};
            }
            const uint8_t cmd = req.data[0];
            size_t n = 0;
            if (cmd == cmd_a) {
                n = n_a;
            } else if (cmd == cmd_b) {
                n = n_b;
            } else {
                return std::vector<Message>{};
            }

            Message reply;
            reply.id = id;
            reply.dlc = static_cast<uint8_t>(1 + n);
            reply.data[0] = cmd;
            if (req.dlc > 1) {
                for (size_t i = 0; i < n; ++i) {
                    reply.data[i + 1] = (i + 1 < req.dlc) ? req.data[i + 1] : fill;
                }
            } else {
                for (size_t i = 0; i < n; ++i) {
                    reply.data[i + 1] = fill;
                }
            }
            return std::vector<Message>{reply};
        };
    }

} // namespace l20lite_comm

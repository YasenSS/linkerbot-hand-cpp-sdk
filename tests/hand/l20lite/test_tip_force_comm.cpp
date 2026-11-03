#include "hand/support/optional_value.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l20lite::kUnknownDirection;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::Side;

namespace {

    constexpr auto kTimeout = std::chrono::milliseconds{500};

    Message channel_reply(uint32_t id, uint8_t cmd, uint8_t fill) {
        Message reply;
        reply.id = id;
        reply.dlc = 6;
        reply.data[0] = cmd;
        for (uint8_t i = 0; i < 5; ++i) {
            reply.data[i + 1] = fill;
        }
        return reply;
    }

    Message finger_reply(uint32_t id, uint8_t cmd) {
        Message reply;
        reply.id = id;
        reply.dlc = 5;
        reply.data[0] = cmd;
        reply.data[1] = 10;
        reply.data[2] = 20;
        reply.data[3] = 64;
        reply.data[4] = 30;
        return reply;
    }

    FakeTransport::Handler tip_handler(uint32_t id) {
        return [id](const Message &req) {
            if (req.dlc < 1) {
                return std::vector<Message>{};
            }
            const uint8_t cmd = req.data[0];
            if (cmd >= 0x20 && cmd <= 0x23) {
                const uint8_t fill = (cmd == 0x22) ? uint8_t{64} : static_cast<uint8_t>(cmd);
                return std::vector<Message>{channel_reply(id, cmd, fill)};
            }
            if (cmd == 0x28 || cmd == 0x29 || cmd == 0x30 || cmd == 0x31 || cmd == 0x32) {
                return std::vector<Message>{finger_reply(id, cmd)};
            }
            return std::vector<Message>{};
        };
    }

} // namespace

// 指尖力：0x20–0x23 五指通道；0x22 的 0–127 → 0–359°，255 为未知。

// get 连发 0x20–0x23；0x22 的 64 → 约 181°（64×359/127）。
TEST_CASE("comm: tip force get_blocking reads 0x20-0x23", "[comm][l20lite][tip_force]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, tip_handler(l20lite_comm::kLeftId));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.tip_force.get_blocking(kTimeout);
    REQUIRE(data.tip_force.thumb.normal == 0x20);
    REQUIRE(data.tip_force.index.tangential == 0x21);
    REQUIRE(data.tip_force.middle.direction_raw == 64);
    REQUIRE(data.tip_force.ring.proximity == 0x23);
    REQUIRE(data.tip_force.thumb.direction_deg().has_value());
    REQUIRE(test_support::require_value(data.tip_force.thumb.direction_deg()) ==
            Catch::Approx(64.0f * 359.0f / 127.0f));

    std::vector<uint8_t> cmds;
    for (const auto &msg : raw->sent()) {
        if (msg.dlc == 1) {
            cmds.push_back(msg.data[0]);
        }
    }
    REQUIRE(cmds.size() >= 4);
}

// 方向字节 255 时 direction_deg() 为空。
TEST_CASE("comm: tip force unknown direction is 255", "[comm][l20lite][tip_force]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId, [](const Message &req) {
        if (req.dlc < 1) {
            return std::vector<Message>{};
        }
        return std::vector<Message>{channel_reply(l20lite_comm::kLeftId, req.data[0], kUnknownDirection)};
    });

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    const auto data = hand.tip_force.get_blocking(kTimeout);
    REQUIRE_FALSE(data.tip_force.thumb.direction_deg().has_value());
}

// get_finger("thumb") 发 0x28、"pinky" 发 0x32；字段 10/20/64/30。
TEST_CASE("comm: tip force get_finger uses 0x28-0x32", "[comm][l20lite][tip_force]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, tip_handler(l20lite_comm::kLeftId));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto thumb = hand.tip_force.get_finger_blocking("thumb", kTimeout);
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{0x28});
    REQUIRE(thumb.normal == 10);
    REQUIRE(thumb.tangential == 20);
    REQUIRE(thumb.direction_raw == 64);
    REQUIRE(thumb.proximity == 30);

    (void)hand.tip_force.get_finger_blocking("pinky", kTimeout);
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{0x32});
}

TEST_CASE("comm: L20Lite tip force rejects conflicting channel fragments", "[comm][l20lite][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, [](const Message &request) {
        auto reply = channel_reply(request.id, request.data[0], 10);
        if (request.data[0] == 0x20) {
            auto conflict = reply;
            conflict.data[1] = 99;
            return std::vector<Message>{reply, conflict};
        }
        return std::vector<Message>{reply};
    });
    L20lite hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    REQUIRE_THROWS(hand.tip_force.get_blocking(std::chrono::milliseconds(100)));
    REQUIRE_FALSE(hand.tip_force.get_snapshot());
    const auto count = raw->sent().size();
    REQUIRE_THROWS(hand.tip_force.get_blocking(std::chrono::milliseconds(20)));
    REQUIRE(raw->sent().size() == count);
}

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l6/angle_manager.hpp>
#include <linkerbot/hand/l6/l6.hpp>
#include <linkerbot/hand/l6/torque_manager.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l6::Angle;
using linkerbot::hand::l6::AngleManager;
using linkerbot::hand::l6::L6;
using linkerbot::hand::l6::Side;
using linkerbot::hand::l6::TorqueManager;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint32_t kRightId = 0x27;
    constexpr uint8_t kAngleCmd = 0x01;
    constexpr uint8_t kTorqueCmd = 0x03;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    float raw_to_percent(uint8_t raw) { return static_cast<float>(raw) * 100.0f / 255.0f; }

    struct ReplyPayloadArguments {
        uint32_t id;
        uint8_t cmd;
        uint8_t fill;
    };
    Message reply_payload(const ReplyPayloadArguments &arguments) {
        Message reply;
        reply.id = arguments.id;
        reply.dlc = 1 + static_cast<uint8_t>(Angle::kJointCount);
        reply.data[0] = arguments.cmd;
        for (size_t i = 0; i < Angle::kJointCount; ++i) {
            reply.data[i + 1] = arguments.fill;
        }
        return reply;
    }

    FakeTransport::Handler device_handler(uint32_t id, uint8_t angle_fill, uint8_t torque_fill) {
        return [id, angle_fill, torque_fill](const Message &req) {
            if (req.dlc < 1) {
                return std::vector<Message>{};
            }
            if (req.data[0] == kAngleCmd) {
                return std::vector<Message>{reply_payload({id, kAngleCmd, angle_fill})};
            }
            if (req.data[0] == kTorqueCmd) {
                return std::vector<Message>{reply_payload({id, kTorqueCmd, torque_fill})};
            }
            return std::vector<Message>{};
        };
    }

} // namespace

// FakeTransport：一个 dispatcher 上左右手、或多 manager 共用总线，靠 CAN ID / 指令码分流。

// 左右手各一个 AngleManager，共用 dispatcher；左 0x28 回 0x40，右 0x27 回 0x60，互不串。
TEST_CASE("comm: two logical devices share one dispatcher", "[comm][l6]") {
    auto transport = std::make_unique<FakeTransport>();
    constexpr uint8_t left_fill = 0x40;
    constexpr uint8_t right_fill = 0x60;
    transport->on_request(kLeftId, device_handler(kLeftId, left_fill, 0));
    transport->echo_cmd(kRightId, kAngleCmd, Angle::kJointCount, right_fill);

    CANDispatcher dispatcher(std::move(transport));

    AngleManager left(kLeftId, dispatcher, test_util::unique_mapping_path());
    AngleManager right(kRightId, dispatcher, test_util::unique_mapping_path());
    left.start();
    right.start();

    const auto left_data = left.get_blocking(kTimeout);
    const auto right_data = right.get_blocking(kTimeout);
    const auto left_arr = left_data.angle.to_array();
    const auto right_arr = right_data.angle.to_array();

    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(left_arr[i] == Catch::Approx(raw_to_percent(left_fill)));
        REQUIRE(right_arr[i] == Catch::Approx(raw_to_percent(right_fill)));
    }

    left.stop();
    right.stop();
    dispatcher.stop();
}

// 同一只左手上角度(0x01)和扭矩(0x03)各一个 manager，按指令码分发给各自。
TEST_CASE("comm: one device fans out to multiple managers", "[comm][l6]") {
    auto transport = std::make_unique<FakeTransport>();
    constexpr uint8_t angle_fill = 0x40;
    constexpr uint8_t torque_fill = 0x80;
    transport->on_request(kLeftId, device_handler(kLeftId, angle_fill, torque_fill));

    CANDispatcher dispatcher(std::move(transport));

    AngleManager angle(kLeftId, dispatcher, test_util::unique_mapping_path());
    TorqueManager torque(kLeftId, dispatcher);
    angle.start();
    torque.start();

    const auto angle_arr = angle.get_blocking(kTimeout).angle.to_array();
    const auto torque_arr = torque.get_blocking(kTimeout).torque.to_array();

    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(angle_arr[i] == Catch::Approx(raw_to_percent(angle_fill)));
        REQUIRE(torque_arr[i] == torque_fill);
    }

    angle.stop();
    torque.stop();
    dispatcher.stop();
}

// L6 可注入 FakeTransport（不必真 SocketCAN），角度/扭矩读回应与填充值一致。
TEST_CASE("comm: L6 accepts an injected FakeTransport", "[comm][l6]") {
    auto transport = std::make_unique<FakeTransport>();
    constexpr uint8_t fill = 0x50;
    transport->on_request(kLeftId, device_handler(kLeftId, fill, 0x20));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto angle_arr = hand.angle.get_blocking(kTimeout).angle.to_array();
    const auto torque_arr = hand.torque.get_blocking(kTimeout).torque.to_array();

    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(angle_arr[i] == Catch::Approx(raw_to_percent(fill)));
        REQUIRE(torque_arr[i] == 0x20);
    }

    REQUIRE_NOTHROW(hand.stop());
}

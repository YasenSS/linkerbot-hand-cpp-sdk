#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20lite/angle_manager.hpp>
#include <linkerbot/hand/l20lite/current_manager.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::hand::l20lite::AngleManager;
using linkerbot::hand::l20lite::CurrentManager;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::Side;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    float raw_to_percent(uint8_t raw) { return static_cast<float>(raw) * 100.0f / 255.0f; }
} // namespace

// FakeTransport：一个 dispatcher 上左右手、或多 manager 共用总线，靠 CAN ID / 指令码分流。

// 左右手各一个 AngleManager，共用 dispatcher；左 0x28 回 0x40，右 0x27 回 0x60，互不串。
TEST_CASE("comm: two logical L20lite devices share one dispatcher", "[comm][l20lite]") {
    auto transport = std::make_unique<FakeTransport>();
    constexpr uint8_t left_fill = 0x40;
    constexpr uint8_t right_fill = 0x60;
    transport->on_request(l20lite_comm::kLeftId,
                          l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x01, 6, 0x04, 4, left_fill));
    transport->on_request(l20lite_comm::kRightId,
                          l20lite_comm::dual_handler(l20lite_comm::kRightId, 0x01, 6, 0x04, 4, right_fill));

    CANDispatcher dispatcher(std::move(transport));

    AngleManager left(l20lite_comm::kLeftId, dispatcher, test_util::unique_mapping_path());
    AngleManager right(l20lite_comm::kRightId, dispatcher, test_util::unique_mapping_path());
    left.start();
    right.start();

    const auto left_arr = left.get_blocking(kTimeout).angle.to_array();
    const auto right_arr = right.get_blocking(kTimeout).angle.to_array();
    for (size_t i = 0; i < left_arr.size(); ++i) {
        REQUIRE(left_arr[i] == Catch::Approx(raw_to_percent(left_fill)));
        REQUIRE(right_arr[i] == Catch::Approx(raw_to_percent(right_fill)));
    }

    left.stop();
    right.stop();
    dispatcher.stop();
}

// 同一只左手上角度(0x01/0x04)和电流(0x02/0x03)各一个 manager，按指令码分发给各自。
TEST_CASE("comm: one L20lite device fans out to multiple managers", "[comm][l20lite]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId, [](const linkerbot::can::Message &req) {
        if (req.dlc < 1) {
            return std::vector<linkerbot::can::Message>{};
        }
        if (req.data[0] == 0x01 || req.data[0] == 0x04) {
            return l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x01, 6, 0x04, 4, 0x40)(req);
        }
        if (req.data[0] == 0x02 || req.data[0] == 0x03) {
            return l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x02, 5, 0x03, 5, 0x80)(req);
        }
        return std::vector<linkerbot::can::Message>{};
    });

    CANDispatcher dispatcher(std::move(transport));

    AngleManager angle(l20lite_comm::kLeftId, dispatcher, test_util::unique_mapping_path());
    CurrentManager current(l20lite_comm::kLeftId, dispatcher);
    angle.start();
    current.start();

    const auto angle_arr = angle.get_blocking(kTimeout).angle.to_array();
    const auto current_arr = current.get_blocking(kTimeout).currents;
    for (size_t i = 0; i < angle_arr.size(); ++i) {
        REQUIRE(angle_arr[i] == Catch::Approx(raw_to_percent(0x40)));
        REQUIRE(current_arr[i] == 0x80);
    }

    angle.stop();
    current.stop();
    dispatcher.stop();
}

// L20lite 可注入 FakeTransport（不必真 SocketCAN），角度/电流读回应与填充值一致。
TEST_CASE("comm: L20lite accepts an injected FakeTransport", "[comm][l20lite]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId, [](const linkerbot::can::Message &req) {
        if (req.dlc < 1) {
            return std::vector<linkerbot::can::Message>{};
        }
        if (req.data[0] == 0x01 || req.data[0] == 0x04) {
            return l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x01, 6, 0x04, 4, 0x50)(req);
        }
        if (req.data[0] == 0x02 || req.data[0] == 0x03) {
            return l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x02, 5, 0x03, 5, 0x20)(req);
        }
        return std::vector<linkerbot::can::Message>{};
    });

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto angle_arr = hand.angle.get_blocking(kTimeout).angle.to_array();
    const auto current_arr = hand.current.get_blocking(kTimeout).currents;
    for (size_t i = 0; i < angle_arr.size(); ++i) {
        REQUIRE(angle_arr[i] == Catch::Approx(raw_to_percent(0x50)));
        REQUIRE(current_arr[i] == 0x20);
    }
    REQUIRE_NOTHROW(hand.stop());
}

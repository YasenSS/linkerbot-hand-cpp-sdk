#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l20lite::Acceleration;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::Side;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
}

// FakeTransport 加速度：0x87/0x88，线性 0–100% ↔ 0–254（与 O6「0=最大」不同）。

// 十关节 100% 应编成 0x87/0x88 两帧，线上都是 254。
TEST_CASE("comm: acceleration 100 percent maps to 254", "[comm][l20lite][acceleration]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x87, 5, 0x88, 5, 0));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.acceleration.set_accelerations(Acceleration(100, 100, 100, 100, 100, 100, 100, 100, 100, 100));
    REQUIRE(l20lite_comm::payloads(raw)[l20lite_comm::payloads(raw).size() - 2] ==
            std::vector<uint8_t>{0x87, 254, 254, 254, 254, 254});
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{0x88, 254, 254, 254, 254, 254});
}

// 十关节 0% 应编成线上 0（与 O6「0=最大」不同）。
TEST_CASE("comm: acceleration 0 maps to 0", "[comm][l20lite][acceleration]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x87, 5, 0x88, 5, 0));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.acceleration.set_accelerations(Acceleration(0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
    REQUIRE(l20lite_comm::last_payload(raw) == std::vector<uint8_t>{0x88, 0, 0, 0, 0, 0});
}

// get_blocking 把线上 254 解成 100%。
TEST_CASE("comm: acceleration get_blocking decodes 254 as 100", "[comm][l20lite][acceleration]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId,
                          l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x87, 5, 0x88, 5, 254));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto arr = hand.acceleration.get_blocking(kTimeout).acceleration.to_array();
    for (auto v : arr) {
        REQUIRE(v == Catch::Approx(100.0f));
    }
}

// 加速度 101、负数、NaN 都应抛 out_of_range。
TEST_CASE("comm: acceleration rejects out-of-range values", "[comm][l20lite][acceleration]") {
    REQUIRE_THROWS_AS(Acceleration(101, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Acceleration(-1, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS_AS(Acceleration(nan, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
}

// timeout<=0 应立刻抛。
TEST_CASE("comm: acceleration get rejects non-positive timeout", "[comm][l20lite][acceleration]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId,
                          l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x87, 5, 0x88, 5, 0));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.acceleration.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: acceleration snapshot populates after get", "[comm][l20lite][acceleration]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId,
                          l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x87, 5, 0x88, 5, 0x40));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.acceleration.get_snapshot().has_value());
    (void)hand.acceleration.get_blocking(kTimeout);
    REQUIRE(hand.acceleration.get_snapshot().has_value());
}

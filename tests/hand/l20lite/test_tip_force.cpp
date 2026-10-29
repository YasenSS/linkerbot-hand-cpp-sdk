#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <linkerbot/hand/l20lite/tip_force.hpp>

#include "hand/l20lite/l20lite_fixture.hpp"

using namespace linkerbot::hand::l20lite;

namespace {

    constexpr auto kTimeout = std::chrono::milliseconds{500};

} // namespace

// 真机指尖力。0x20–0x23 五指通道；direction_raw ≤127 或 255（未知）。

// 一次读齐五指；to_array 大小 5，方向字节合法。
TEST_CASE("TipForce: get_blocking returns all fingers", "[l20lite][sensor][tip_force]") {
    auto &hand = test_util::l20lite_hand();
    auto data = hand.tip_force.get_blocking(kTimeout);
    auto arr = data.tip_force.to_array();
    REQUIRE(arr.size() == TipForce::kFingerCount);
    for (const auto &finger : arr) {
        REQUIRE((finger.direction_raw <= 127 || finger.direction_raw == kUnknownDirection));
    }
}

// 五个名字都能 get_finger_blocking，不抛。
TEST_CASE("TipForce: each finger accessible by name", "[l20lite][sensor][tip_force]") {
    auto &hand = test_util::l20lite_hand();
    const std::vector<std::string> fingers = {"thumb", "index", "middle", "ring", "pinky"};
    for (const auto &name : fingers) {
        REQUIRE_NOTHROW(hand.tip_force.get_finger_blocking(name, kTimeout));
    }
}

// 读过之后，manager 与整机快照里 tip_force 都应有值。
TEST_CASE("TipForce: snapshot populated after read", "[l20lite][sensor][tip_force]") {
    auto &hand = test_util::l20lite_hand();
    (void)hand.tip_force.get_blocking(kTimeout);
    REQUIRE(hand.tip_force.get_snapshot().has_value());
    REQUIRE(hand.get_snapshot().tip_force.has_value());
}

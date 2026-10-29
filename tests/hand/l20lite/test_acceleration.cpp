#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <thread>

#include "hand/l20lite/l20lite_fixture.hpp"

using namespace linkerbot::hand::l20lite;

static constexpr float kTolerance = 15.0f;

// 真机加速度。0x87+0x88；用户 0–100% 线性对应线上 0–254。

// set_accelerations(Acceleration) 十关节写 80%，下发不抛。
TEST_CASE("Acceleration: set_accelerations with struct", "[l20lite][control][acceleration]") {
    auto &hand = test_util::l20lite_hand();
    REQUIRE_NOTHROW(hand.acceleration.set_accelerations(Acceleration::from_array(test_util::all_joints(80))));
}

// 当前加速度十个值都应在 [0, 100]。
TEST_CASE("Acceleration: get_blocking returns valid range", "[l20lite][control][acceleration]") {
    auto &hand = test_util::l20lite_hand();
    auto arr = hand.acceleration.get_blocking(std::chrono::milliseconds{500}).acceleration.to_array();
    for (auto v : arr) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 100.0f);
    }
}

// 十关节都设 50%，读回都≈50（真机跟不准，允许 ±15%）。
TEST_CASE("Acceleration: set and read within tolerance", "[l20lite][control][acceleration]") {
    auto &hand = test_util::l20lite_hand();
    hand.acceleration.set_accelerations(Acceleration::from_array(test_util::all_joints(50)));
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    auto arr = hand.acceleration.get_blocking(std::chrono::milliseconds{500}).acceleration.to_array();
    for (auto v : arr) {
        REQUIRE(std::abs(v - 50.0f) < kTolerance);
    }
}

// 另一个重载 set_accelerations(array)：十关节都设 70%，下发不抛。
TEST_CASE("Acceleration: set_accelerations with std::array", "[l20lite][control][acceleration]") {
    auto &hand = test_util::l20lite_hand();
    REQUIRE_NOTHROW(hand.acceleration.set_accelerations(test_util::all_joints(70)));
}

// get_blocking 之后，整机快照里 acceleration 应有值。
TEST_CASE("Acceleration: snapshot populated after read", "[l20lite][control][acceleration]") {
    auto &hand = test_util::l20lite_hand();
    (void)hand.acceleration.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().acceleration.has_value());
}

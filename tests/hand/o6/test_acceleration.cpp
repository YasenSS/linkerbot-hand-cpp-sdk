#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <thread>

#include "hand/o6/o6_fixture.hpp"

using namespace linkerbot::hand::o6;

static constexpr float kTolerance = 15.0f;

// 真机加速度。0x87 读写；用户 100% 对应线上 0（最大），0–99 对应 1–254。

// set_accelerations(Acceleration) 六关节写 80%，下发不抛。
TEST_CASE("Acceleration: set_accelerations with struct", "[o6][control][acceleration]") {
    auto &hand = test_util::o6_hand();
    REQUIRE_NOTHROW(hand.acceleration.set_accelerations(Acceleration(80, 80, 80, 80, 80, 80)));
}

// 当前加速度六个值都应在 [0, 100]。
TEST_CASE("Acceleration: get_blocking returns valid range", "[o6][control][acceleration]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.acceleration.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.acceleration.to_array();
    for (size_t i = 0; i < Acceleration::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 100.0f);
    }
}

// 六关节都设 50%，读回都≈50（真机跟不准，允许 ±15%）。
TEST_CASE("Acceleration: set and read within tolerance", "[o6][control][acceleration]") {
    auto &hand = test_util::o6_hand();
    hand.acceleration.set_accelerations(Acceleration(50, 50, 50, 50, 50, 50));
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    auto arr = hand.acceleration.get_blocking(std::chrono::milliseconds{500}).acceleration.to_array();
    for (size_t i = 0; i < Acceleration::kJointCount; ++i) {
        REQUIRE(std::abs(arr[i] - 50.0f) < kTolerance);
    }
}

// get_blocking 之后 snapshot.acceleration 应有值。
TEST_CASE("Acceleration: snapshot populated after read", "[o6][control][acceleration]") {
    auto &hand = test_util::o6_hand();
    (void)hand.acceleration.get_blocking(std::chrono::milliseconds{500});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.acceleration.has_value());
}

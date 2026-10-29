#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <thread>

#include "hand/l6/l6_fixture.hpp"

using namespace linkerbot::hand::l6;

static constexpr auto kTimeout = std::chrono::milliseconds{500};
static constexpr float kTimeTolerance = 15.0f;
static constexpr float kLimitTolerance = 15.0f;

// 真机堵转。0xC5 时间、0xC6 阈值、0xC7 扭矩；set 后读回，测完写回原值，不 save。

// 读堵转时间，六个值都应在 [0, 2550] ms。
TEST_CASE("Stall: get_times_blocking returns valid range", "[l6][control][stall]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.stall.get_times_blocking(kTimeout);
    auto arr = data.time.to_array();
    for (size_t i = 0; i < StallTime::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= StallTime::kMaxMs);
    }
}

// 读堵转阈值，六个值都应在 [0, 1400]。
TEST_CASE("Stall: get_thresholds_blocking returns valid range", "[l6][control][stall]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.stall.get_thresholds_blocking(kTimeout);
    auto arr = data.limit.to_array();
    for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= StallLimit::kMax);
    }
}

// 读堵转扭矩，六个值都应在 [0, 1400]。
TEST_CASE("Stall: get_torques_blocking returns valid range", "[l6][control][stall]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.stall.get_torques_blocking(kTimeout);
    auto arr = data.limit.to_array();
    for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= StallLimit::kMax);
    }
}

// 写成 100 ms 再读回，容差内应≈100，然后恢复原值。
TEST_CASE("Stall: set and read times", "[l6][control][stall]") {
    auto &hand = test_util::l6_hand();
    const auto original = hand.stall.get_times_blocking(kTimeout).time;
    const StallTime target(100, 100, 100, 100, 100, 100);

    hand.stall.set_times(target);
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    auto actual = hand.stall.get_times_blocking(kTimeout).time.to_array();
    hand.stall.set_times(original);

    for (size_t i = 0; i < StallTime::kJointCount; ++i) {
        REQUIRE(std::abs(actual[i] - 100.0f) < kTimeTolerance);
    }
}

// 写成 400 再读回，容差内应≈400，然后恢复原值。
TEST_CASE("Stall: set and read thresholds", "[l6][control][stall]") {
    auto &hand = test_util::l6_hand();
    const auto original = hand.stall.get_thresholds_blocking(kTimeout).limit;
    const StallLimit target(400, 400, 400, 400, 400, 400);

    hand.stall.set_thresholds(target);
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    auto actual = hand.stall.get_thresholds_blocking(kTimeout).limit.to_array();
    hand.stall.set_thresholds(original);

    for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
        REQUIRE(std::abs(actual[i] - 400.0f) < kLimitTolerance);
    }
}

// 写成 800 再读回，容差内应≈800，然后恢复原值。
TEST_CASE("Stall: set and read torques", "[l6][control][stall]") {
    auto &hand = test_util::l6_hand();
    const auto original = hand.stall.get_torques_blocking(kTimeout).limit;
    const StallLimit target(800, 800, 800, 800, 800, 800);

    hand.stall.set_torques(target);
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    auto actual = hand.stall.get_torques_blocking(kTimeout).limit.to_array();
    hand.stall.set_torques(original);

    for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
        REQUIRE(std::abs(actual[i] - 800.0f) < kLimitTolerance);
    }
}

// get 之后 stall 自己的 snapshot 应有值（不进 L6::get_snapshot）。
TEST_CASE("Stall: snapshots populated after read", "[l6][control][stall]") {
    auto &hand = test_util::l6_hand();
    (void)hand.stall.get_times_blocking(kTimeout);
    (void)hand.stall.get_thresholds_blocking(kTimeout);
    (void)hand.stall.get_torques_blocking(kTimeout);

    REQUIRE(hand.stall.get_time_snapshot().has_value());
    REQUIRE(hand.stall.get_threshold_snapshot().has_value());
    REQUIRE(hand.stall.get_torque_snapshot().has_value());
}

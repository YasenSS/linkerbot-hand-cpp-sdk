#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <thread>

#include "hand/o6/o6_fixture.hpp"

using namespace linkerbot::hand::o6;

static constexpr auto kTimeout = std::chrono::milliseconds{500};
static constexpr float kTimeTolerance = 15.0f;
static constexpr float kLimitTolerance = 20.0f;

// 真机堵转。新协议 0xC5 阈值、0xC6 时间、0xC7 扭矩；set 后读回，测完写回原值，不 save。

// 堵转时间六个值都应在 [0, kMaxMs]。
TEST_CASE("Stall: get_times_blocking returns valid range", "[o6][control][stall]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.stall.get_times_blocking(kTimeout);
    auto arr = data.time.to_array();
    for (size_t i = 0; i < StallTime::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= StallTime::kMaxMs);
    }
}

// 堵转电流阈值六个值都应在 [0, kMax]。
TEST_CASE("Stall: get_thresholds_blocking returns valid range", "[o6][control][stall]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.stall.get_thresholds_blocking(kTimeout);
    auto arr = data.limit.to_array();
    for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= StallLimit::kMax);
    }
}

// 堵转扭矩六个值都应在 [0, kMax]。
TEST_CASE("Stall: get_torques_blocking returns valid range", "[o6][control][stall]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.stall.get_torques_blocking(kTimeout);
    auto arr = data.limit.to_array();
    for (size_t i = 0; i < StallLimit::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= StallLimit::kMax);
    }
}

// 时间写成 100 ms，读回≈100，再写回原值。
TEST_CASE("Stall: set and read times", "[o6][control][stall]") {
    auto &hand = test_util::o6_hand();
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

// 阈值写成 400 mA，读回≈400，再写回原值。
TEST_CASE("Stall: set and read thresholds", "[o6][control][stall]") {
    auto &hand = test_util::o6_hand();
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

// 扭矩写成 800 mA，读回≈800，再写回原值。
TEST_CASE("Stall: set and read torques", "[o6][control][stall]") {
    auto &hand = test_util::o6_hand();
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

// 三种 get 之后 time/threshold/torque snapshot 都有值。
TEST_CASE("Stall: snapshots populated after read", "[o6][control][stall]") {
    auto &hand = test_util::o6_hand();
    (void)hand.stall.get_times_blocking(kTimeout);
    (void)hand.stall.get_thresholds_blocking(kTimeout);
    (void)hand.stall.get_torques_blocking(kTimeout);

    REQUIRE(hand.stall.get_time_snapshot().has_value());
    REQUIRE(hand.stall.get_threshold_snapshot().has_value());
    REQUIRE(hand.stall.get_torque_snapshot().has_value());
}

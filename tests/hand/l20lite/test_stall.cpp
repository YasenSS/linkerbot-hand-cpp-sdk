#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <thread>

#include "hand/l20lite/l20lite_fixture.hpp"

using namespace linkerbot::hand::l20lite;

static constexpr auto kTimeout = std::chrono::milliseconds{500};
static constexpr float kTimeTolerance = 15.0f;
static constexpr float kLimitTolerance = 8.0f;

namespace {
    // Firmware has no opcode-only stall read, so tests cannot snapshot factory
    // values. Restore to the mid-range defaults used on this unit (500 ms / 50%).
    StallTime default_times() { return StallTime::from_array(test_util::all_joints(500)); }

    StallLimit default_limits() { return StallLimit::from_array(test_util::all_joints(50)); }
} // namespace

// 真机堵转。0xC5 阈值、0xC6 时间、0xC7 扭矩，BYTE1 分银行；get 靠 7 字节回显。

// 先写 500 ms 再读，十个值都应在 [0, 2550] ms。
TEST_CASE("Stall: get_times_blocking returns valid range", "[l20lite][control][stall]") {
    auto &hand = test_util::l20lite_hand();
    hand.stall.set_times(default_times());
    auto arr = hand.stall.get_times_blocking(kTimeout).time.to_array();
    for (auto v : arr) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= StallTime::kMaxMs);
    }
}

// 十关节写成 100 ms 再读回，容差内应≈100，然后恢复默认。
TEST_CASE("Stall: set and read times", "[l20lite][control][stall]") {
    auto &hand = test_util::l20lite_hand();
    hand.stall.set_times(StallTime::from_array(test_util::all_joints(100)));
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    auto actual = hand.stall.get_times_blocking(kTimeout).time.to_array();
    hand.stall.set_times(default_times());

    for (auto v : actual) {
        REQUIRE(std::abs(v - 100.0f) < kTimeTolerance);
    }
}

// 十关节阈值写成 40 再读回，容差内应≈40，然后恢复默认。
TEST_CASE("Stall: set and read thresholds", "[l20lite][control][stall]") {
    auto &hand = test_util::l20lite_hand();
    hand.stall.set_thresholds(StallLimit::from_array(test_util::all_joints(40)));
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    auto actual = hand.stall.get_thresholds_blocking(kTimeout).limit.to_array();
    hand.stall.set_thresholds(default_limits());

    for (auto v : actual) {
        REQUIRE(std::abs(v - 40.0f) < kLimitTolerance);
    }
}

// 三种 get 之后 time/threshold/torque snapshot 都有值。
TEST_CASE("Stall: snapshots populated after read", "[l20lite][control][stall]") {
    auto &hand = test_util::l20lite_hand();
    hand.stall.set_times(default_times());
    hand.stall.set_thresholds(default_limits());
    hand.stall.set_torques(default_limits());
    (void)hand.stall.get_times_blocking(kTimeout);
    (void)hand.stall.get_thresholds_blocking(kTimeout);
    (void)hand.stall.get_torques_blocking(kTimeout);
    REQUIRE(hand.stall.get_time_snapshot().has_value());
    REQUIRE(hand.stall.get_threshold_snapshot().has_value());
    REQUIRE(hand.stall.get_torque_snapshot().has_value());
}

// 先写 50% 再读，十个阈值都应在 [0, StallLimit::kMax]。
TEST_CASE("Stall: get_thresholds_blocking returns valid range", "[l20lite][control][stall]") {
    auto &hand = test_util::l20lite_hand();
    hand.stall.set_thresholds(default_limits());
    auto arr = hand.stall.get_thresholds_blocking(kTimeout).limit.to_array();
    for (auto v : arr) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= StallLimit::kMax);
    }
}

// 先写 50% 再读，十个扭矩都应在 [0, StallLimit::kMax]。
TEST_CASE("Stall: get_torques_blocking returns valid range", "[l20lite][control][stall]") {
    auto &hand = test_util::l20lite_hand();
    hand.stall.set_torques(default_limits());
    auto arr = hand.stall.get_torques_blocking(kTimeout).limit.to_array();
    for (auto v : arr) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= StallLimit::kMax);
    }
}

// 十关节扭矩写成 40 再读回，容差内应≈40，然后恢复默认。
TEST_CASE("Stall: set and read torques", "[l20lite][control][stall]") {
    auto &hand = test_util::l20lite_hand();
    hand.stall.set_torques(StallLimit::from_array(test_util::all_joints(40)));
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    auto actual = hand.stall.get_torques_blocking(kTimeout).limit.to_array();
    hand.stall.set_torques(default_limits());

    for (auto v : actual) {
        REQUIRE(std::abs(v - 40.0f) < kLimitTolerance);
    }
}

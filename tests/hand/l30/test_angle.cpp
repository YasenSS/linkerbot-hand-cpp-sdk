#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

#include "hand/l30/l30_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l30;

// 真机角度。17 关节百分比，0 % 是 spec.minimum、100 % 是 spec.maximum；
// from_sensor_raw 不夹取，真机过冲时读回会略微超出 [0, 100]，所以按放宽区间判。

// 读回 17 个百分比，都应落在放宽后的合理区间内。
TEST_CASE("Angle: get_blocking returns plausible range", "[l30][control][angle]") {
    auto &hand = test_util::l30_hand();
    hand.control.enable(std::chrono::milliseconds{1000});
    const auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    for (size_t i = 0; i < kJointCount; ++i) {
        INFO("joint[" << i << "]=" << std::string(joint_name(i)));
        REQUIRE(data.angles[i] >= test_util::kAngleReadLowerBound);
        REQUIRE(data.angles[i] <= test_util::kAngleReadUpperBound);
    }
}

// 中立姿态（全 50 %）不该抛，读回也仍在合理区间。
TEST_CASE("Angle: neutral pose is accepted", "[l30][control][angle]") {
    auto &hand = test_util::l30_hand();
    hand.control.enable(std::chrono::milliseconds{1000});
    REQUIRE_NOTHROW(hand.angle.set_angles_percent(test_util::l30_neutral_pose()));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    const auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    for (size_t i = 0; i < kJointCount; ++i) {
        INFO("joint[" << i << "]=" << std::string(joint_name(i)));
        REQUIRE(data.angles[i] >= test_util::kAngleReadLowerBound);
        REQUIRE(data.angles[i] <= test_util::kAngleReadUpperBound);
    }
}
TEST_CASE("L30Angle=0", "[l30][control][angle]") {
    auto &hand = test_util::l30_hand();
    hand.control.enable(std::chrono::milliseconds{1000});
    REQUIRE_NOTHROW(hand.angle.set_angles_percent(test_util::l30_open_pose()));
    std::this_thread::sleep_for(std::chrono::seconds{2});
    const auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    for (size_t i = 0; i < kJointCount; ++i) {
        INFO("joint[" << i << "]=" << std::string(joint_name(i)));
        REQUIRE(data.angles[i] >= test_util::kAngleReadLowerBound);
        REQUIRE(data.angles[i] <= test_util::kAngleReadUpperBound);
    }
}
// get_blocking 之后，整机快照里 angle 应有值。
TEST_CASE("Angle: snapshot populated after read", "[l30][control][angle]") {
    auto &hand = test_util::l30_hand();
    (void)hand.angle.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().angle.has_value());
}

// 人眼确认全开、全握；结束回中立姿态。
TEST_CASE("Angle: interactive open and close", "[l30][control][angle][interactive]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    test_util::InteractiveSession session("l30_angle_open_close");
    session
        .step(
            "Hand will fully open", [&] { test_util::move_and_wait_l30(hand, test_util::l30_open_pose()); },
            "All fingers should extend")
        .step(
            "Hand will close", [&] { test_util::move_and_wait_l30(hand, test_util::l30_closed_pose()); },
            "All fingers should flex");

    session.run();
    session.save_report();

    test_util::move_and_wait_l30(hand, test_util::l30_neutral_pose());

    REQUIRE(session.failed_steps().empty());
}

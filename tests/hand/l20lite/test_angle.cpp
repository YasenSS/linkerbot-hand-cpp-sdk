#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <thread>

#include "hand/l20lite/l20lite_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l20lite;

// 真机角度。0x01+0x04 十关节；thumb_abd 常停在 100。百分比 0–100，允许 ±15%。
static constexpr float kTolerance = 15.0f;

// 手指张开，侧摆先收到最小再打到最大。
TEST_CASE("Angle: test_angle", "[l20lite][control][angle]") {
    auto &hand = test_util::l20lite_hand();

    hand.angle.set_angles_percent(Angle(
        /*thumb_flex*/ 100, /*thumb_abd*/ 0,
        /*index_flex*/ 100, /*middle*/ 100, /*ring*/ 100, /*pinky*/ 100,
        /*index_abd*/ 0, /*ring_abd*/ 0, /*pinky_abd*/ 0,
        /*thumb_yaw*/ 100));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    hand.angle.set_angles_percent(Angle(
        /*thumb_flex*/ 100, /*thumb_abd*/ 100,
        /*index_flex*/ 100, /*middle*/ 100, /*ring*/ 100, /*pinky*/ 100,
        /*index_abd*/ 100, /*ring_abd*/ 100, /*pinky_abd*/ 100,
        /*thumb_yaw*/ 100));
    std::this_thread::sleep_for(std::chrono::seconds{2});
}

// 问当前角度，十个值都应在 [0, 100]。
TEST_CASE("Angle: get_blocking returns valid range", "[l20lite][control][angle]") {
    auto &hand = test_util::l20lite_hand();
    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.angle.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 100.0f);
    }
}

// set_angles_percent(Angle) 全开姿态不抛；读回拇指屈曲、食指屈曲都≈100。
TEST_CASE("Angle: set_angles with Angle struct", "[l20lite][control][angle]") {
    auto &hand = test_util::l20lite_hand();
    REQUIRE_NOTHROW(hand.angle.set_angles_percent(test_util::open_pose()));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(std::abs(data.angle.thumb_flex - 100.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.index_flex - 100.0f) < kTolerance);
}

// set_angles_percent(array)：十关节都设 50，读回都≈50。
TEST_CASE("Angle: set_angles with std::array", "[l20lite][control][angle]") {
    auto &hand = test_util::l20lite_hand();
    const auto target = test_util::all_joints(50);
    REQUIRE_NOTHROW(hand.angle.set_angles_percent(target));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto arr = hand.angle.get_blocking(std::chrono::milliseconds{500}).angle.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(std::abs(arr[i] - 50.0f) < kTolerance);
    }
}

// 行程一端：closed_pose 全握，读回都在容差内。
TEST_CASE("Angle: set all closed and read back", "[l20lite][control][angle]") {
    auto &hand = test_util::l20lite_hand();
    const auto target = test_util::closed_pose();
    hand.angle.set_angles_percent(target);
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto actual = hand.angle.get_blocking(std::chrono::milliseconds{500}).angle.to_array();
    auto expected = target.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(std::abs(actual[i] - expected[i]) < kTolerance);
    }
}

// 新 L20lite 还没读过，整机快照里角度应为空。
TEST_CASE("Angle: snapshot none before read", "[l20lite][control][angle]") {
    const L20lite fresh_hand(test_util::get_l20lite_side(), test_util::get_interface());
    auto snap = fresh_hand.get_snapshot();
    REQUIRE_FALSE(snap.angle.has_value());
}

// get_blocking 之后，get_snapshot().angle 应有值。
TEST_CASE("Angle: snapshot populated after read", "[l20lite][control][angle]") {
    auto &hand = test_util::l20lite_hand();
    (void)hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
}

// 人眼确认全开、全握。
TEST_CASE("Angle: interactive full open and close", "[l20lite][control][angle][interactive]") {
    auto &hand = test_util::l20lite_hand();

    test_util::InteractiveSession session("l20lite_angle_open_close");
    session
        .step(
            "Hand will fully open", [&] { test_util::move_and_wait(hand, test_util::open_pose()); },
            "All fingers should be fully open")
        .step(
            "Hand will fully close", [&] { test_util::move_and_wait(hand, test_util::closed_pose()); },
            "All fingers should be fully closed");

    session.run();
    session.save_report();
    REQUIRE(session.failed_steps().empty());
}

// 从握拳起按拇→食依次张开，再全握。
TEST_CASE("Angle: interactive individual finger movement", "[l20lite][control][angle][interactive]") {
    auto &hand = test_util::l20lite_hand();
    test_util::move_and_wait(hand, test_util::closed_pose());

    test_util::InteractiveSession session("l20lite_angle_individual_fingers");
    session
        .step(
            "Thumb will extend",
            [&] { test_util::move_and_wait(hand, Angle(100, 100, 0, 0, 0, 0, 100, 100, 100, 100)); },
            "Only the thumb should open")
        .step(
            "Index finger will extend",
            [&] { test_util::move_and_wait(hand, Angle(100, 100, 100, 0, 0, 0, 50, 100, 100, 100)); },
            "Thumb stays open, index opens without folding onto the thumb")
        .step(
            "All fingers close", [&] { test_util::move_and_wait(hand, test_util::closed_pose()); },
            "All fingers should close");

    session.run();
    session.save_report();
    REQUIRE(session.failed_steps().empty());
}

// 行程另一端：open_pose 全开，读回十关节都在容差内。
TEST_CASE("Angle: set all open and read back", "[l20lite][control][angle]") {
    auto &hand = test_util::l20lite_hand();
    const auto target = test_util::open_pose();
    hand.angle.set_angles_percent(target);
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto actual = hand.angle.get_blocking(std::chrono::milliseconds{500}).angle.to_array();
    auto expected = target.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(std::abs(actual[i] - expected[i]) < kTolerance);
    }
}

// 只改食指屈曲，拇指屈曲和中指屈曲应仍停在 50。
TEST_CASE("Angle: set individual finger", "[l20lite][control][angle]") {
    auto &hand = test_util::l20lite_hand();

    hand.angle.set_angles_percent(Angle(50, 100, 50, 50, 50, 50, 50, 50, 50, 50));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    hand.angle.set_angles_percent(Angle(50, 100, 100, 50, 50, 50, 50, 50, 50, 50));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(std::abs(data.angle.index_flex - 100.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.thumb_flex - 50.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.middle_flex - 50.0f) < kTolerance);
}

// 0→25→50→75→100，看中间档位是否平滑；pct==0 时拇指屈曲 20，防卡住食指。
TEST_CASE("Angle: interactive gradual movement", "[l20lite][control][angle][interactive]") {
    auto &hand = test_util::l20lite_hand();

    test_util::InteractiveSession session("l20lite_angle_gradual");
    constexpr std::array pcts = {0.f, 25.f, 50.f, 75.f, 100.f};
    for (const float pct : pcts) {
        const std::string label = "Move all fingers to " + std::to_string(static_cast<int>(pct)) + "%";
        const std::string expected = "Fingers at approximately " + std::to_string(static_cast<int>(pct)) + "% open";
        session.step(
            label,
            [&hand, pct] {
                const float thumb_flex = (pct == 0.f) ? 20.f : pct;
                test_util::move_and_wait(hand, Angle(thumb_flex, 100, pct, pct, pct, pct, pct, pct, pct, 100));
            },
            expected);
    }

    session.run();
    session.save_report();
    REQUIRE(session.failed_steps().empty());
}

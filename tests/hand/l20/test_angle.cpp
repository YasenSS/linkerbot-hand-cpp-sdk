#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <string>
#include <thread>

#include "hand/l20/l20_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l20;

// 真机角度。0x41–0x45 五帧十六关节；百分比 0–100，关键关节允许 ±15%。
static constexpr float kTolerance = 15.0f;

namespace {

    void restore_open(L20 &hand) { test_util::move_and_wait(hand, test_util::open_pose()); }

} // namespace

// 真机热身：先全握再全开，确认五帧下发能带动十六关节。
TEST_CASE("Angle: test_angle", "[l20][control][angle]") {
    auto &hand = test_util::l20_hand();

    hand.angle.set_angles_percent(test_util::closed_pose());
    std::this_thread::sleep_for(std::chrono::seconds{2});

    hand.angle.set_angles_percent(test_util::open_pose());
    std::this_thread::sleep_for(std::chrono::seconds{2});
}

// 问当前角度，十六个值都应在 [0, 100]。
TEST_CASE("Angle: get_blocking returns valid range", "[l20][control][angle]") {
    auto &hand = test_util::l20_hand();
    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.angle.to_array();
    REQUIRE(arr.size() == Angle::kJointCount);
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 100.0f);
    }
}

// set_angles_percent(Angle) 全开姿态不抛；读回拇指 root1、食指 root1 都≈100。
TEST_CASE("Angle: set_angles with Angle struct", "[l20][control][angle]") {
    auto &hand = test_util::l20_hand();
    REQUIRE_NOTHROW(hand.angle.set_angles_percent(test_util::open_pose()));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(std::abs(data.angle.thumb_root1 - 100.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.index_root1 - 100.0f) < kTolerance);
}

// set_angles_percent(array)：全开数组下发不抛，关键屈曲仍≈100。
TEST_CASE("Angle: set_angles with std::array", "[l20][control][angle]") {
    auto &hand = test_util::l20_hand();
    const auto target = test_util::open_pose().to_array();
    REQUIRE_NOTHROW(hand.angle.set_angles_percent(target));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(std::abs(data.angle.thumb_root1 - 100.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.index_root1 - 100.0f) < kTolerance);
}

// 行程一端：closed_pose 全握，食指/中指屈曲读回≈0。
TEST_CASE("Angle: set all closed and read back", "[l20][control][angle]") {
    auto &hand = test_util::l20_hand();
    hand.angle.set_angles_percent(test_util::closed_pose());
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(std::abs(data.angle.index_root1 - 0.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.index_tip - 0.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.middle_root1 - 0.0f) < kTolerance);
}

// 行程另一端：open_pose 全开，拇指/食指 root1 读回≈100。
TEST_CASE("Angle: set all open and read back", "[l20][control][angle]") {
    auto &hand = test_util::l20_hand();
    hand.angle.set_angles_percent(test_util::open_pose());
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(std::abs(data.angle.thumb_root1 - 100.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.index_root1 - 100.0f) < kTolerance);
}

// 其余保持张开，只握食指；食指 root1≈0，拇指/中指 root1 仍≈100。
TEST_CASE("Angle: set individual finger", "[l20][control][angle]") {
    auto &hand = test_util::l20_hand();

    hand.angle.set_angles_percent(test_util::open_pose());
    std::this_thread::sleep_for(std::chrono::seconds{2});

    hand.angle.set_angles_percent(Angle(100, 100, 100, 100, 50, 0, 0, 67, 100, 100, 33, 100, 100, 0, 100, 100));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(std::abs(data.angle.index_root1 - 0.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.thumb_root1 - 100.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.middle_root1 - 100.0f) < kTolerance);
}

// 新 L20 还没读过，整机快照里角度应为空。
TEST_CASE("Angle: snapshot none before read", "[l20][control][angle]") {
    const L20 fresh_hand(test_util::get_l20_side(), test_util::get_interface());
    auto snap = fresh_hand.get_snapshot();
    REQUIRE_FALSE(snap.angle.has_value());
}

// get_blocking 之后，get_snapshot().angle 应有值。
TEST_CASE("Angle: snapshot populated after read", "[l20][control][angle]") {
    auto &hand = test_util::l20_hand();
    (void)hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
}

// 人眼确认全开、全握。
TEST_CASE("Angle: interactive full open and close", "[l20][control][angle][interactive]") {
    auto &hand = test_util::l20_hand();

    test_util::InteractiveSession session("l20_angle_open_close");
    session
        .step(
            "Hand will fully open", [&] { test_util::move_and_wait(hand, test_util::open_pose()); },
            "All fingers should be fully open")
        .step(
            "Hand will fully close", [&] { test_util::move_and_wait(hand, test_util::closed_pose()); },
            "All fingers should be fully closed");

    session.run();
    session.save_report();
    restore_open(hand);
    REQUIRE(session.failed_steps().empty());
}

// 从握拳起按拇→食→中→无名→小依次张开，再全握。
TEST_CASE("Angle: interactive individual finger movement", "[l20][control][angle][interactive]") {
    auto &hand = test_util::l20_hand();
    test_util::move_and_wait(hand, test_util::closed_pose());

    test_util::InteractiveSession session("l20_angle_individual_fingers");
    session
        .step(
            "Thumb will extend",
            [&] { test_util::move_and_wait(hand, Angle(100, 100, 100, 100, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0)); },
            "Only the thumb should open")
        .step(
            "Index finger will extend",
            [&] {
                test_util::move_and_wait(hand, Angle(100, 100, 100, 100, 100, 100, 100, 50, 0, 0, 50, 0, 0, 50, 0, 0));
            },
            "Thumb stays open, index opens")
        .step(
            "Middle finger will extend",
            [&] {
                test_util::move_and_wait(hand,
                                         Angle(100, 100, 100, 100, 100, 100, 100, 67, 100, 100, 50, 0, 0, 50, 0, 0));
            },
            "Thumb + index stay open, middle opens")
        .step(
            "Ring finger will extend",
            [&] {
                test_util::move_and_wait(
                    hand, Angle(100, 100, 100, 100, 100, 100, 100, 67, 100, 100, 33, 100, 100, 50, 0, 0));
            },
            "Thumb + index + middle stay open, ring opens")
        .step(
            "Pinky finger will extend", [&] { test_util::move_and_wait(hand, test_util::open_pose()); },
            "All fingers should be open")
        .step(
            "All fingers close", [&] { test_util::move_and_wait(hand, test_util::closed_pose()); },
            "All fingers should close");

    session.run();
    session.save_report();
    restore_open(hand);
    REQUIRE(session.failed_steps().empty());
}

// 0→25→50→75→100，在全握与全开之间插值，看中间档位是否平滑。
TEST_CASE("Angle: interactive gradual movement", "[l20][control][angle][interactive]") {
    auto &hand = test_util::l20_hand();

    test_util::InteractiveSession session("l20_angle_gradual");
    constexpr std::array pcts = {0.f, 25.f, 50.f, 75.f, 100.f};
    for (const float pct : pcts) {
        const std::string label = "Move all fingers to " + std::to_string(static_cast<int>(pct)) + "%";
        const std::string expected = "Fingers at approximately " + std::to_string(static_cast<int>(pct)) + "% open";
        session.step(
            label,
            [&hand, pct] {
                const auto open = test_util::open_pose().to_array();
                const auto closed = test_util::closed_pose().to_array();
                std::array<float, Angle::kJointCount> values{};
                for (size_t i = 0; i < Angle::kJointCount; ++i) {
                    values[i] = closed[i] + (open[i] - closed[i]) * (pct / 100.0f);
                }
                test_util::move_and_wait(hand, Angle::from_array(values));
            },
            expected);
    }

    session.run();
    session.save_report();
    restore_open(hand);
    REQUIRE(session.failed_steps().empty());
}

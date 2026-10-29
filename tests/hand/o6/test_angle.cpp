#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <thread>

#include "hand/o6/o6_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::o6;

// 真机角度测试。百分比 0–100，顺序：拇指屈曲、外展、食、中、无名、小指。
// 真机跟不准，允许 ±15%。
static constexpr float kTolerance = 15.0f;

// 当前角度六个值都应在 [0, 100]。
TEST_CASE("Angle: get_blocking returns valid range", "[o6][control][angle]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.angle.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 100.0f);
    }
}

// set_angles_percent(Angle)：拇指屈 50、外展 100，读回这两轴都在容差内。
TEST_CASE("Angle: set_angles with Angle struct", "[o6][control][angle]") {
    auto &hand = test_util::o6_hand();
    REQUIRE_NOTHROW(hand.angle.set_angles_percent(Angle(50, 100, 50, 50, 50, 50)));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.angle.to_array();
    REQUIRE(std::abs(arr[0] - 50.0f) < kTolerance);
    REQUIRE(std::abs(arr[1] - 100.0f) < kTolerance);
}

// set_angles_percent(array)：六指都设 60%，读回都≈60。
TEST_CASE("Angle: set_angles with std::array", "[o6][control][angle]") {
    auto &hand = test_util::o6_hand();
    const std::array<float, 6> target = {60, 60, 60, 60, 60, 60};
    REQUIRE_NOTHROW(hand.angle.set_angles_percent(target));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.angle.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(std::abs(arr[i] - 60.0f) < kTolerance);
    }
}

// 六个目标各不相同，读回都在容差内。
TEST_CASE("Angle: set and read within tolerance", "[o6][control][angle]") {
    auto &hand = test_util::o6_hand();
    const Angle target(30, 100, 40, 50, 60, 80);
    hand.angle.set_angles_percent(target);
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto actual = data.angle.to_array();
    auto expected = target.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(std::abs(actual[i] - expected[i]) < kTolerance);
    }
}

// 握拳位（屈 0、拇指外展 100）写完能读回。
TEST_CASE("Angle: set all closed and read back", "[o6][control][angle]") {
    auto &hand = test_util::o6_hand();
    const Angle target(0, 100, 0, 0, 0, 0);
    hand.angle.set_angles_percent(target);
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto actual = data.angle.to_array();
    auto expected = target.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(std::abs(actual[i] - expected[i]) < kTolerance);
    }
}

// 全开 100% 写完能读回。
TEST_CASE("Angle: set all open and read back", "[o6][control][angle]") {
    auto &hand = test_util::o6_hand();
    const Angle target(100, 100, 100, 100, 100, 100);
    hand.angle.set_angles_percent(target);
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto actual = data.angle.to_array();
    auto expected = target.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(std::abs(actual[i] - expected[i]) < kTolerance);
    }
}

// 先半开再只把食指打到 100%，食指到位、其余保持。
TEST_CASE("Angle: set individual finger", "[o6][control][angle]") {
    auto &hand = test_util::o6_hand();

    hand.angle.set_angles_percent(Angle(50, 100, 50, 50, 50, 50));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    hand.angle.set_angles_percent(Angle(50, 100, 100, 50, 50, 50));
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto data = hand.angle.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(std::abs(data.angle.index - 100.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.thumb_flex - 50.0f) < kTolerance);
    REQUIRE(std::abs(data.angle.middle - 50.0f) < kTolerance);
}

// 新实例尚未读角度时 snapshot.angle 为空。
TEST_CASE("Angle: snapshot none before read", "[o6][control][angle]") {
    const O6 fresh_hand(test_util::get_o6_side(), test_util::get_interface());
    auto snap = fresh_hand.get_snapshot();
    REQUIRE_FALSE(snap.angle.has_value());
}

// get_blocking 之后 snapshot.angle 应有值。
TEST_CASE("Angle: snapshot populated after read", "[o6][control][angle]") {
    auto &hand = test_util::o6_hand();
    (void)hand.angle.get_blocking(std::chrono::milliseconds{500});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
}

// 人眼确认：先全开再握拳。
TEST_CASE("Angle: interactive full open and close", "[o6][control][angle][interactive]") {
    auto &hand = test_util::o6_hand();

    test_util::InteractiveSession session("angle_open_close");
    session
        .step(
            "Hand will fully open", [&] { test_util::move_and_wait(hand, Angle(100, 100, 100, 100, 100, 100)); },
            "All fingers should be fully open")
        .step(
            "Hand will fully close", [&] { test_util::move_and_wait(hand, Angle(0, 100, 0, 0, 0, 0)); },
            "All fingers should be fully closed");

    session.run();
    session.save_report();

    REQUIRE(session.failed_steps().empty());
}

// 人眼确认：从握拳起逐指张开，再全部合上。
TEST_CASE("Angle: interactive individual finger movement", "[o6][control][angle][interactive]") {
    auto &hand = test_util::o6_hand();

    test_util::move_and_wait(hand, Angle(0, 100, 0, 0, 0, 0));

    test_util::InteractiveSession session("angle_individual_fingers");
    session
        .step(
            "Thumb will extend", [&] { test_util::move_and_wait(hand, Angle(100, 100, 0, 0, 0, 0)); },
            "Only the thumb should open")
        .step(
            "Index finger will extend", [&] { test_util::move_and_wait(hand, Angle(100, 100, 100, 0, 0, 0)); },
            "Thumb stays open, index opens")
        .step(
            "Middle finger will extend", [&] { test_util::move_and_wait(hand, Angle(100, 100, 100, 100, 0, 0)); },
            "Thumb + index stay open, middle opens")
        .step(
            "Ring finger will extend", [&] { test_util::move_and_wait(hand, Angle(100, 100, 100, 100, 100, 0)); },
            "Thumb + index + middle stay open, ring opens")
        .step(
            "Pinky finger will extend", [&] { test_util::move_and_wait(hand, Angle(100, 100, 100, 100, 100, 100)); },
            "All fingers should be open")
        .step(
            "All fingers close", [&] { test_util::move_and_wait(hand, Angle(0, 100, 0, 0, 0, 0)); },
            "All fingers should close");

    session.run();
    session.save_report();

    REQUIRE(session.failed_steps().empty());
}

// 人眼确认：0/25/50/75/100% 分档张开。
TEST_CASE("Angle: interactive gradual movement", "[o6][control][angle][interactive]") {
    auto &hand = test_util::o6_hand();

    test_util::InteractiveSession session("angle_gradual");
    constexpr std::array pcts = {0.f, 25.f, 50.f, 75.f, 100.f};
    for (const float pct : pcts) {
        const std::string label = "Move all fingers to " + std::to_string(static_cast<int>(pct)) + "%";
        const std::string expected = "Fingers at approximately " + std::to_string(static_cast<int>(pct)) + "% open";
        session.step(
            label, [&hand, pct] { test_util::move_and_wait(hand, Angle(pct, 100, pct, pct, pct, pct)); }, expected);
    }

    session.run();
    session.save_report();

    REQUIRE(session.failed_steps().empty());
}

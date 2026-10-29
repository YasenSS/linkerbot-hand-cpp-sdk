#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>

#include "hand/l6/l6_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l6;

static constexpr float kTolerance = 15.0f;

// 真机运动扭矩。0x02 读写，0–100%；真机跟不准，允许 ±15%。结束把扭矩打回 100%。

// --- 自动：程序读回扭矩自己判 ---

// 当前扭矩六个值都应在 [0, 100]。
TEST_CASE("Torque: get_blocking returns valid range", "[l6][control][torque]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.torque.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.torque.to_array();
    for (size_t i = 0; i < Torque::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 100.0f);
    }
}

// set_torques(Torque) 六关节 50%，读回都≈50。
TEST_CASE("Torque: set_torques with Torque struct", "[l6][control][torque]") {
    auto &hand = test_util::l6_hand();
    REQUIRE_NOTHROW(hand.torque.set_torques(Torque(50, 50, 50, 50, 50, 50)));
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    auto data = hand.torque.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.torque.to_array();
    for (size_t i = 0; i < Torque::kJointCount; ++i) {
        REQUIRE(std::abs(arr[i] - 50.0f) < kTolerance);
    }
}

// 另一个重载 set_torques(array)：六指都设 70%，读回都≈70。
TEST_CASE("Torque: set_torques with std::array", "[l6][control][torque]") {
    auto &hand = test_util::l6_hand();
    const std::array<uint8_t, 6> target = {70, 70, 70, 70, 70, 70};
    REQUIRE_NOTHROW(hand.torque.set_torques(target));
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    auto data = hand.torque.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.torque.to_array();
    for (size_t i = 0; i < Torque::kJointCount; ++i) {
        REQUIRE(std::abs(arr[i] - 70.0f) < kTolerance);
    }
}

// 六个目标各不相同，读回都在容差内。
TEST_CASE("Torque: set and read within tolerance", "[l6][control][torque]") {
    auto &hand = test_util::l6_hand();
    const Torque target(20, 40, 60, 80, 30, 50);
    hand.torque.set_torques(target);
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    auto data = hand.torque.get_blocking(std::chrono::milliseconds{500});
    auto actual = data.torque.to_array();
    auto expected = target.to_array();
    for (size_t i = 0; i < Torque::kJointCount; ++i) {
        REQUIRE(std::abs(actual[i] - expected[i]) < kTolerance);
    }
}

// get_blocking 之后，整机快照里 torque 应有值。
TEST_CASE("Torque: snapshot populated after read", "[l6][control][torque]") {
    auto &hand = test_util::l6_hand();
    (void)hand.torque.get_blocking(std::chrono::milliseconds{500});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.torque.has_value());
}

// --- 交互：先 20% 弱握再 100% 强握，人拉物体感受力度 ---

namespace {
    constexpr auto kPromptWait = std::chrono::milliseconds{3000};

    void wait_after_prompt() {
        std::cout << "3 秒后开始动作，请按提示做好准备…\n" << std::flush;
        std::this_thread::sleep_for(kPromptWait);
    }
} // namespace

TEST_CASE("Torque: interactive torque affects grip strength", "[l6][control][torque][interactive]") {
    auto &hand = test_util::l6_hand();

    test_util::InteractiveSession session("torque_grip_strength");
    session
        .step(
            "请把物体放进手里；即将用 20% 弱力矩握紧",
            [&] {
                wait_after_prompt();
                hand.torque.set_torques(Torque(20, 20, 20, 20, 20, 20));
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                test_util::move_and_wait(hand, Angle(0, 100, 0, 0, 0, 0), 3);
            },
            "应是弱握，物体比较容易被拉出")
        .step(
            "即将先张开再以 100% 大力矩握紧",
            [&] {
                wait_after_prompt();
                hand.torque.set_torques(Torque(100, 100, 100, 100, 100, 100));
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                test_util::move_and_wait(hand, Angle(100, 100, 100, 100, 100, 100), 2);
                test_util::move_and_wait(hand, Angle(0, 100, 0, 0, 0, 0), 3);
            },
            "应是强握，物体比较难被拉出");

    session.run();
    session.save_report();

    hand.torque.set_torques(Torque(100, 100, 100, 100, 100, 100));

    REQUIRE(session.failed_steps().empty());
}

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <thread>

#include "hand/o6/o6_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::o6;

// 真机电流。0x02 空载荷查询读取实测值，带数据写入电流限制。

// 当前电流返回六个原始值；空闲时读回接近 0 也正常。
TEST_CASE("Current: get_blocking returns raw values", "[o6][sensor][current]") {
    auto &hand = test_util::o6_hand();
    const auto data = hand.current.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(data.currents.size() == kCurrentJointCount);
}

// 六关节写约 50% 的电流限制，下发不抛。
TEST_CASE("Current limit: set uniform raw values", "[o6][control][current-limit]") {
    auto &hand = test_util::o6_hand();
    const std::array<uint8_t, 6> target{128, 128, 128, 128, 128, 128};
    REQUIRE_NOTHROW(hand.current_limit.set_current_limits(target));
}

// 六指都设约 70% 的电流限制，下发不抛。
TEST_CASE("Current limit: set std::array", "[o6][control][current-limit]") {
    auto &hand = test_util::o6_hand();
    const std::array<uint8_t, 6> target{179, 179, 179, 179, 179, 179};
    REQUIRE_NOTHROW(hand.current_limit.set_current_limits(target));
}

// 六个目标各不相同，下发不抛。
TEST_CASE("Current limit: set different per-motor values", "[o6][control][current-limit]") {
    auto &hand = test_util::o6_hand();
    const std::array<uint8_t, 6> target{51, 102, 153, 204, 77, 128};
    REQUIRE_NOTHROW(hand.current_limit.set_current_limits(target));
}

// get_blocking 之后 snapshot.current 应有值。
TEST_CASE("Current: snapshot populated after read", "[o6][sensor][current]") {
    auto &hand = test_util::o6_hand();
    (void)hand.current.get_blocking(std::chrono::milliseconds{500});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.current.has_value());
}

namespace {
    constexpr auto kPromptWait = std::chrono::milliseconds{3000};

    // 交互步里提示后等 3 秒再动手。
    void wait_after_prompt() {
        std::cout << "3 秒后开始动作，请按提示做好准备…\n" << std::flush;
        std::this_thread::sleep_for(kPromptWait);
    }
} // namespace

// 人眼确认：20% 弱握能拉开，100% 强握难拉开；结束写回 100%。
TEST_CASE("Current limit: interactive limit affects grip strength", "[o6][control][current-limit][interactive]") {
    auto &hand = test_util::o6_hand();

    test_util::InteractiveSession session("current_limit_grip_strength");
    session
        .step(
            "请把物体放进手里；即将用 20% 弱力矩握紧",
            [&] {
                wait_after_prompt();
                hand.current_limit.set_current_limits({51, 51, 51, 51, 51, 51});
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                test_util::move_and_wait(hand, Angle(0, 100, 0, 0, 0, 0), 3);
            },
            "应是弱握，物体比较容易被拉出")
        .step(
            "即将先张开再以 100% 大力矩握紧",
            [&] {
                wait_after_prompt();
                hand.current_limit.set_current_limits({255, 255, 255, 255, 255, 255});
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                test_util::move_and_wait(hand, Angle(100, 100, 100, 100, 100, 100), 2);
                test_util::move_and_wait(hand, Angle(0, 100, 0, 0, 0, 0), 3);
            },
            "应是强握，物体比较难被拉出");

    session.run();
    session.save_report();

    hand.current_limit.set_current_limits({255, 255, 255, 255, 255, 255});

    REQUIRE(session.failed_steps().empty());
}

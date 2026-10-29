#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>
#include <thread>

#include "hand/l20/l20_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l20;

// 真机电流。0x51–0x55 五帧查询实测值，写入则设置电流限制。

// 当前电流返回十六个原始值。
TEST_CASE("Current: get_blocking returns raw values", "[l20][sensor][current]") {
    auto &hand = test_util::l20_hand();
    const auto data = hand.current.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(data.currents.size() == kJointCount);
}

// get_blocking 之后，整机快照里 current 应有值。
TEST_CASE("Current: snapshot populated after read", "[l20][sensor][current]") {
    auto &hand = test_util::l20_hand();
    (void)hand.current.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().current.has_value());
}

// 十六关节设置约 50% 的电流限制，下发不抛。
TEST_CASE("Current limit: set uniform values", "[l20][control][current-limit]") {
    auto &hand = test_util::l20_hand();
    REQUIRE_NOTHROW(hand.current_limit.set_current_limits(percents_to_raw(test_util::all_joints(50))));
}

// 十六关节都设置约 70% 的电流限制，下发不抛。
TEST_CASE("Current limit: set std::array", "[l20][control][current-limit]") {
    auto &hand = test_util::l20_hand();
    REQUIRE_NOTHROW(hand.current_limit.set_current_limits(percents_to_raw(test_util::all_joints(70))));
}

namespace {
    constexpr auto kPromptWait = std::chrono::milliseconds{3000};

    void wait_after_prompt() {
        std::cout << "3 秒后开始动作，请按提示做好准备…\n" << std::flush;
        std::this_thread::sleep_for(kPromptWait);
    }
} // namespace

// 人眼确认：20% 弱握能拉开，100% 强握难拉开；结束写回 100% 并张开。
TEST_CASE("Current limit: interactive limit affects grip strength", "[l20][control][current-limit][interactive]") {
    auto &hand = test_util::l20_hand();

    test_util::InteractiveSession session("l20_current_limit_grip_strength");
    session
        .step(
            "请把物体放进手里；即将用 20% 弱力矩握紧",
            [&] {
                wait_after_prompt();
                hand.current_limit.set_current_limits(percents_to_raw(test_util::all_joints(20)));
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                test_util::move_and_wait(hand, test_util::closed_pose(), 3);
            },
            "应是弱握，物体比较容易被拉出")
        .step(
            "即将先张开再以 100% 大力矩握紧",
            [&] {
                wait_after_prompt();
                hand.current_limit.set_current_limits(percents_to_raw(test_util::all_joints(100)));
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                test_util::move_and_wait(hand, test_util::open_pose(), 2);
                test_util::move_and_wait(hand, test_util::closed_pose(), 3);
            },
            "应是强握，物体比较难被拉出");

    session.run();
    session.save_report();

    hand.current_limit.set_current_limits(percents_to_raw(test_util::all_joints(100)));
    test_util::move_and_wait(hand, test_util::open_pose());

    REQUIRE(session.failed_steps().empty());
}

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>
#include <thread>

#include "hand/l20lite/l20lite_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l20lite;

// 真机电流。0x02+0x03 空载荷查询实测值，带数据写入电流限制。

// 当前电流返回十个原始值。
TEST_CASE("Current: get_blocking returns raw values", "[l20lite][sensor][current]") {
    auto &hand = test_util::l20lite_hand();
    const auto data = hand.current.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(data.currents.size() == kJointCount);
}

// 十关节写约 50% 的电流限制，下发不抛。
TEST_CASE("Current limit: set uniform values", "[l20lite][control][current-limit]") {
    auto &hand = test_util::l20lite_hand();
    REQUIRE_NOTHROW(hand.current_limit.set_current_limits(percents_to_raw(test_util::all_joints(50))));
}

// get_blocking 之后，整机快照里 current 应有值。
TEST_CASE("Current: snapshot populated after read", "[l20lite][sensor][current]") {
    auto &hand = test_util::l20lite_hand();
    (void)hand.current.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().current.has_value());
}

// 十关节约 50% 的电流限制。
TEST_CASE("Current limit: set converted percent values", "[l20lite][control][current-limit]") {
    auto &hand = test_util::l20lite_hand();
    REQUIRE_NOTHROW(hand.current_limit.set_current_limits(percents_to_raw(test_util::all_joints(50))));
}

// 十关节都设置约 70% 的电流限制，下发不抛。
TEST_CASE("Current limit: set std::array", "[l20lite][control][current-limit]") {
    auto &hand = test_util::l20lite_hand();
    REQUIRE_NOTHROW(hand.current_limit.set_current_limits(percents_to_raw(test_util::all_joints(70))));
}

namespace {
    constexpr auto kPromptWait = std::chrono::milliseconds{3000};

    void wait_after_prompt() {
        std::cout << "3 秒后开始动作，请按提示做好准备…\n" << std::flush;
        std::this_thread::sleep_for(kPromptWait);
    }
} // namespace

// 人眼确认：20% 弱握能拉开，100% 强握难拉开；结束写回 100%。
TEST_CASE("Current limit: interactive limit affects grip strength", "[l20lite][control][current-limit][interactive]") {
    auto &hand = test_util::l20lite_hand();

    test_util::InteractiveSession session("l20lite_current_limit_grip_strength");
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

    REQUIRE(session.failed_steps().empty());
}

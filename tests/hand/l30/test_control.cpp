#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "hand/l30/l30_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l30;

namespace {
    constexpr auto kAckTimeout = std::chrono::milliseconds{1000};
}

// 真机全局使能/失能。两条都是带 ACK 的写命令，成功即返回，失败会带上设备状态字
// （0x20 未解锁、0x22 电机未使能、0x23 当前状态不允许）。自动用例只确认握手能过，
// 使能后关节是否真的有力要靠最后的交互用例人工确认。用例结束一律回到失能状态。

// 使能一次应当顺利拿到 ACK 不抛异常。
TEST_CASE("Control: enable is acknowledged", "[l30][control][enable]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_NOTHROW(hand.control.enable(kAckTimeout));
    REQUIRE_NOTHROW(hand.control.disable(kAckTimeout));
}

// 失能一次应当顺利拿到 ACK 不抛异常。
TEST_CASE("Control: disable is acknowledged", "[l30][control][enable]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_NOTHROW(hand.control.disable(kAckTimeout));
}

// 使能→失能→再使能→再失能，反复切换不应报错，最后停在失能。
TEST_CASE("Control: enable and disable can be repeated", "[l30][control][enable]") {
    auto &hand = test_util::l30_hand();
    for (int round = 0; round < 2; ++round) {
        REQUIRE_NOTHROW(hand.control.enable(kAckTimeout));
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        REQUIRE_NOTHROW(hand.control.disable(kAckTimeout));
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
    }
}

// 非正超时是本地参数校验，真机上同样立刻抛出，不会发帧。
TEST_CASE("Control: rejects non-positive timeout", "[l30][control][enable]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_THROWS_AS(hand.control.enable(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.control.disable(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

// --- 交互：会驱动真实关节，人工看现象后按 y/n ---

// 失能时手指应能被轻松掰动，使能后应明显变硬并能走到中立位；结束回到失能。
TEST_CASE("Control: interactive enable holds the joints", "[l30][control][enable][interactive]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    test_util::InteractiveSession session("l30_control_enable_holds");

    session
        .step(
            "先失能，请用手轻轻掰动任意一根手指",
            [&] {
                hand.control.disable(kAckTimeout);
                std::cout << "已发送失能，关节应处于可自由掰动状态\n";
                std::this_thread::sleep_for(std::chrono::seconds{2});
            },
            "失能后手指应能被轻松掰动")
        .step(
            "现在使能并走到中立位，请再试着掰动同一根手指",
            [&] {
                hand.control.enable(kAckTimeout);
                std::cout << "已发送使能，即将走到中立位\n";
                test_util::move_and_wait_l30(hand, test_util::l30_neutral_pose());
            },
            "使能后手指应明显变硬、能保持在中立位")
        .step(
            "结束，回到失能",
            [&] {
                hand.control.disable(kAckTimeout);
                std::cout << "已发送失能\n";
                std::this_thread::sleep_for(std::chrono::seconds{1});
            },
            "手应重新变为可自由掰动");

    session.run();
    session.save_report();

    REQUIRE(session.failed_steps().empty());
}

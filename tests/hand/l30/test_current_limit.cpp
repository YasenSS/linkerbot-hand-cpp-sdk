#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>

#include "hand/l30/l30_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l30;

// 真机电流限制。原始计数 60..800，6.5 mA 每计数；只写不读，快照存的是刚下发的命令值。

// set_all 在边界 60 与 800 都不该抛。
TEST_CASE("Current limit: accepts range boundaries", "[l30][control][current-limit]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_NOTHROW(hand.current_limit.set_all(kCurrentLimitMin));
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    REQUIRE_NOTHROW(hand.current_limit.set_all(kCurrentLimitMax));
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
}

// 越界计数（59、801）该在发送前被拒。
TEST_CASE("Current limit: rejects out of range counts", "[l30][control][current-limit]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_THROWS_AS(hand.current_limit.set_all(static_cast<uint16_t>(kCurrentLimitMin - 1)),
                      linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.current_limit.set_all(static_cast<uint16_t>(kCurrentLimitMax + 1)),
                      linkerbot::hand::ValidationError);
}

// 协议没有电流限制读回：写完之后快照就是刚下发的那组命令值。
TEST_CASE("Current limit: snapshot holds commanded value", "[l30][control][current-limit]") {
    auto &hand = test_util::l30_hand();
    hand.current_limit.set_all(300);
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    const auto snapshot = hand.current_limit.get_snapshot();
    REQUIRE(snapshot.has_value());
    for (const auto counts : test_support::require_value(snapshot).current_limits) {
        REQUIRE(counts == 300);
    }
    REQUIRE(hand.get_snapshot().current_limit.has_value());
}

// 人眼确认：小电流限制握力弱、大电流限制握力强；结束回中立姿态并写回中等限制。
TEST_CASE("Current limit: interactive limit affects grip strength", "[l30][control][current-limit][interactive]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    test_util::InteractiveSession session("l30_current_limit_grip_strength");
    session
        .step(
            "请把物体放进手里；即将用 60 计数弱力矩握紧",
            [&] {
                hand.current_limit.set_all(kCurrentLimitMin);
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                test_util::move_and_wait_l30(hand, test_util::l30_closed_pose(), 3);
            },
            "应是弱握，物体比较容易被拉出")
        .step(
            "即将先张开再以 800 计数大力矩握紧",
            [&] {
                hand.current_limit.set_all(kCurrentLimitMax);
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                test_util::move_and_wait_l30(hand, test_util::l30_open_pose(), 2);
                test_util::move_and_wait_l30(hand, test_util::l30_closed_pose(), 3);
            },
            "应是强握，物体比较难被拉出");

    session.run();
    session.save_report();

    hand.current_limit.set_all(300);
    test_util::move_and_wait_l30(hand, test_util::l30_neutral_pose());

    REQUIRE(session.failed_steps().empty());
}

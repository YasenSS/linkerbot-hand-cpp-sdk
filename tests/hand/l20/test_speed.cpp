#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "hand/l20/l20_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l20;

// 真机速度。0x49–0x4D 五帧可写可读；读回是当前转速不是设定值，静止时可能是 0。
// 设定是否生效靠下面交互看手指。

// set_speeds(Speed) 十六关节写 50%，下发不抛。
TEST_CASE("Speed: set_speeds with Speed struct", "[l20][control][speed]") {
    auto &hand = test_util::l20_hand();
    REQUIRE_NOTHROW(hand.speed.set_speeds(Speed::from_array(test_util::all_joints(50))));
}

// 当前速度十六个值都应在 [0, 100]；静止时读回接近 0 也正常。
TEST_CASE("Speed: get_blocking returns valid range", "[l20][control][speed]") {
    auto &hand = test_util::l20_hand();
    auto arr = hand.speed.get_blocking(std::chrono::milliseconds{500}).speed.to_array();
    REQUIRE(arr.size() == Speed::kJointCount);
    for (auto v : arr) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 100.0f);
    }
}

// 另一个重载 set_speeds(array)：十六关节都设 70%，下发不抛。
TEST_CASE("Speed: set_speeds with std::array", "[l20][control][speed]") {
    auto &hand = test_util::l20_hand();
    REQUIRE_NOTHROW(hand.speed.set_speeds(test_util::all_joints(70)));
}

// 十六个电机速度各不相同，确认不是只能写同一值。
TEST_CASE("Speed: set different per-motor speeds", "[l20][control][speed]") {
    auto &hand = test_util::l20_hand();
    REQUIRE_NOTHROW(hand.speed.set_speeds(Speed(20, 40, 60, 80, 100, 50, 30, 70, 90, 10, 25, 45, 65, 85, 55, 35)));
}

// 人眼确认：10% 慢张、100% 快张；结束把速度打回 100%，手张开。
TEST_CASE("Speed: interactive speed affects movement", "[l20][control][speed][interactive]") {
    auto &hand = test_util::l20_hand();

    test_util::InteractiveSession session("l20_speed_effect");
    session
        .step(
            "Set low speed (10%) and move fingers",
            [&] {
                hand.speed.set_speeds(test_util::all_joints(10));
                test_util::move_and_wait(hand, test_util::closed_pose(), 3);
                hand.angle.set_angles_percent(test_util::open_pose());
                std::this_thread::sleep_for(std::chrono::seconds{5});
            },
            "Fingers should move SLOWLY to open position")
        .step(
            "Set high speed (100%) and move fingers",
            [&] {
                hand.speed.set_speeds(test_util::all_joints(100));
                test_util::move_and_wait(hand, test_util::closed_pose(), 3);
                hand.angle.set_angles_percent(test_util::open_pose());
                std::this_thread::sleep_for(std::chrono::seconds{3});
            },
            "Fingers should move FAST to open position");

    session.run();
    session.save_report();

    hand.speed.set_speeds(test_util::all_joints(100));
    test_util::move_and_wait(hand, test_util::open_pose());

    REQUIRE(session.failed_steps().empty());
}

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <thread>

#include "hand/l6/l6_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l6;

// 真机速度。0x05 只写不读，自动用例只检查 set 不抛错；快慢靠下面交互看手指。

// --- 自动：set_speeds 发得出去 ---

// Speed 结构体，六关节都设 50%。
TEST_CASE("Speed: set_speeds with Speed struct", "[l6][control][speed]") {
    auto &hand = test_util::l6_hand();
    REQUIRE_NOTHROW(hand.speed.set_speeds(Speed(50, 50, 50, 50, 50, 50)));
}

// 另一个重载 set_speeds(array)：六指都设 70%。
TEST_CASE("Speed: set_speeds with std::array", "[l6][control][speed]") {
    auto &hand = test_util::l6_hand();
    const std::array<float, 6> target = {70, 70, 70, 70, 70, 70};
    REQUIRE_NOTHROW(hand.speed.set_speeds(target));
}

// 六个电机速度各不相同，确认不是只能写同一值。
TEST_CASE("Speed: set different per-motor speeds", "[l6][control][speed]") {
    auto &hand = test_util::l6_hand();
    REQUIRE_NOTHROW(hand.speed.set_speeds(Speed(10, 20, 30, 40, 50, 60)));
}

// --- 交互：先 10% 慢张、再 100% 快张，人眼看快慢；结束把速度打回 100%。

TEST_CASE("Speed: interactive speed affects movement", "[l6][control][speed][interactive]") {
    auto &hand = test_util::l6_hand();

    test_util::InteractiveSession session("speed_effect");
    session
        .step(
            "Set low speed (10%) and move fingers",
            [&] {
                hand.speed.set_speeds(Speed(10, 10, 10, 10, 10, 10));
                test_util::move_and_wait(hand, Angle(0, 100, 0, 0, 0, 0), 3);
                hand.angle.set_angles_percent(Angle(100, 100, 100, 100, 100, 100));
                std::this_thread::sleep_for(std::chrono::seconds{5});
            },
            "Fingers should move SLOWLY to open position")
        .step(
            "Set high speed (100%) and move fingers",
            [&] {
                hand.speed.set_speeds(Speed(100, 100, 100, 100, 100, 100));
                test_util::move_and_wait(hand, Angle(0, 100, 0, 0, 0, 0), 3);
                hand.angle.set_angles_percent(Angle(100, 100, 100, 100, 100, 100));
                std::this_thread::sleep_for(std::chrono::seconds{3});
            },
            "Fingers should move FAST to open position");

    session.run();
    session.save_report();

    // Reset speed to default
    hand.speed.set_speeds(Speed(100, 100, 100, 100, 100, 100));

    REQUIRE(session.failed_steps().empty());
}

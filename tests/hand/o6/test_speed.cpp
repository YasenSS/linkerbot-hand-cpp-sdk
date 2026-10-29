#include <catch2/catch_test_macros.hpp>

#include <array>
#include <thread>

#include "hand/o6/o6_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::o6;

// 真机速度。0x05 可写可读；读回是当前转速不是设定值，静止时可能是 0。
// 设定是否生效靠下面交互看手指。先把加速度打到最大，否则 10%/100% 看不出差别。

// set_speeds(Speed) 六关节写 50%，下发不抛。
TEST_CASE("Speed: set_speeds with Speed struct", "[o6][control][speed]") {
    auto &hand = test_util::o6_hand();
    REQUIRE_NOTHROW(hand.speed.set_speeds(Speed(50, 50, 50, 50, 50, 50)));
}

// 另一个重载 set_speeds(array)：六指都设 70%，下发不抛。
TEST_CASE("Speed: set_speeds with std::array", "[o6][control][speed]") {
    auto &hand = test_util::o6_hand();
    const std::array<float, 6> target = {70, 70, 70, 70, 70, 70};
    REQUIRE_NOTHROW(hand.speed.set_speeds(target));
}

// 六个目标各不相同，下发不抛。
TEST_CASE("Speed: set different per-motor speeds", "[o6][control][speed]") {
    auto &hand = test_util::o6_hand();
    REQUIRE_NOTHROW(hand.speed.set_speeds(Speed(10, 20, 30, 40, 50, 60)));
}

// 当前速度六个值都应在 [0, 100]；静止时读回接近 0 也正常。
TEST_CASE("Speed: get_blocking returns valid range", "[o6][control][speed]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.speed.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.speed.to_array();
    for (size_t i = 0; i < Speed::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 100.0f);
    }
}

// 人眼确认：加速度先拉满，10% 张开慢、100% 张开快；结束写回 100%。
TEST_CASE("Speed: interactive speed affects movement", "[o6][control][speed][interactive]") {
    auto &hand = test_util::o6_hand();
    hand.acceleration.set_accelerations(Acceleration(100, 100, 100, 100, 100, 100));
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

    hand.speed.set_speeds(Speed(100, 100, 100, 100, 100, 100));

    REQUIRE(session.failed_steps().empty());
}

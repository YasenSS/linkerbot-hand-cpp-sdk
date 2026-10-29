#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>

#include "hand/l30/l30_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::l30;

namespace {
    constexpr auto kAckTimeout = std::chrono::milliseconds{1000};
    constexpr auto kSampleTimeout = std::chrono::milliseconds{100};

    // 人眼对比用中低/中高档，1 和 250 差太大：1 计数几乎看不出在动。
    constexpr uint16_t kSlowCounts = 20;
    constexpr uint16_t kFastCounts = 200;
    constexpr uint16_t kDefaultCounts = 100;

    // 下发姿态后在 wait 窗口内反复读当前速度（0x5/0x03），不是命令回读。
    uint16_t peak_speed_while_moving(L30 &hand, const Angle &pose, std::chrono::seconds wait) {
        hand.angle.set_angles_percent(pose);
        uint16_t peak = 0;
        const auto deadline = std::chrono::steady_clock::now() + wait;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto data = hand.speed.get_blocking(kSampleTimeout);
            for (size_t i = 0; i < kJointCount; ++i) {
                peak = std::max(peak, data.speeds[i]);
            }
        }
        return peak;
    }
} // namespace

// 真机速度。写 0x1/0x03 是无 ACK 的目标速度；读 0x5/0x03 是当前转速。
// 静止读回 0 合法。要证明速度命令生效，必须先使能再走姿态，看运动中计数抬起来。

// 静止读回应 ≤250；0 表示没在转，不是失败。
TEST_CASE("Speed: get_blocking returns plausible counts", "[l30][control][speed]") {
    auto &hand = test_util::l30_hand();
    const auto data = hand.speed.get_blocking(std::chrono::milliseconds{500});
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(data.speeds[i] <= kSpeedMax);
    }
}

// set_all 在边界 1 与 250 都不该抛。这只说明帧发出去了，手不必动。
TEST_CASE("Speed: accepts range boundaries", "[l30][control][speed]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_NOTHROW(hand.speed.set_all(kSpeedMin));
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    REQUIRE_NOTHROW(hand.speed.set_all(kSpeedMax));
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    hand.speed.set_all(kDefaultCounts);
}

// 越界计数（0、251）该在发送前被拒。
TEST_CASE("Speed: rejects out of range counts", "[l30][control][speed]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_THROWS_AS(hand.speed.set_all(0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.set_all(static_cast<uint16_t>(kSpeedMax + 1)), linkerbot::hand::ValidationError);
}

// 使能后走开合，运动窗口内至少有一个关节当前速度 > 0。
TEST_CASE("Speed: counts rise while joints move", "[l30][control][speed]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_NOTHROW(hand.control.enable(kAckTimeout));
    hand.speed.set_all(kFastCounts);

    const auto opening = peak_speed_while_moving(hand, test_util::l30_open_pose(), std::chrono::seconds{2});
    const auto closing = peak_speed_while_moving(hand, test_util::l30_closed_pose(), std::chrono::seconds{2});
    const auto peak = std::max(opening, closing);

    hand.speed.set_all(kDefaultCounts);
    test_util::move_and_wait_l30(hand, test_util::l30_neutral_pose());

    REQUIRE(peak > 0);
}

// get_blocking 之后，整机快照里 speed 应有值。
TEST_CASE("Speed: snapshot populated after read", "[l30][control][speed]") {
    auto &hand = test_util::l30_hand();
    (void)hand.speed.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().speed.has_value());
}

// 人眼确认低速开合慢、高速开合快；结束恢复默认速度并回中立。
TEST_CASE("Speed: interactive speed affects movement", "[l30][control][speed][interactive]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);
    REQUIRE_NOTHROW(hand.control.enable(kAckTimeout));

    test_util::InteractiveSession session("l30_speed_affects_movement");
    session
        .step(
            "即将以 20 计数低速张开",
            [&] {
                hand.speed.set_all(kSlowCounts);
                test_util::move_and_wait_l30(hand, test_util::l30_closed_pose(), 1);
                test_util::move_and_wait_l30(hand, test_util::l30_open_pose(), 5);
            },
            "手指应明显缓慢张开")
        .step(
            "即将以 200 计数高速握拢",
            [&] {
                hand.speed.set_all(kFastCounts);
                test_util::move_and_wait_l30(hand, test_util::l30_closed_pose(), 2);
            },
            "手指应明显更快握拢");

    session.run();
    session.save_report();

    hand.speed.set_all(kDefaultCounts);
    test_util::move_and_wait_l30(hand, test_util::l30_neutral_pose());

    REQUIRE(session.failed_steps().empty());
}

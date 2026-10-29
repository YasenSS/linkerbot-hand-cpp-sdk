#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <thread>

#include <linkerbot/hand/l30/events.hpp>
#include <linkerbot/hand/l30/l30.hpp>

#include "hand/l30/l30_fixture.hpp"

using namespace linkerbot::hand::l30;

// 真机轮询。start_polling 每个源起一条线程发阻塞读，结果进 snapshot。

// 只轮询角度，1s 后 snapshot.angle 应有值。
TEST_CASE("Polling: single source populates snapshot", "[l30][polling]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    std::this_thread::sleep_for(std::chrono::seconds{1});
    REQUIRE(hand.get_snapshot().angle.has_value());
}

// 同时轮询角度、速度、电流、温度、故障，五份 snapshot 都应有值。
TEST_CASE("Polling: multiple sources populate snapshots", "[l30][polling]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({
        {SensorSource::kAngle, std::chrono::milliseconds{50}},
        {SensorSource::kSpeed, std::chrono::milliseconds{50}},
        {SensorSource::kCurrent, std::chrono::milliseconds{50}},
        {SensorSource::kTemperature, std::chrono::milliseconds{50}},
        {SensorSource::kFault, std::chrono::milliseconds{50}},
    });
    std::this_thread::sleep_for(std::chrono::seconds{2});

    const auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
    REQUIRE(snap.speed.has_value());
    REQUIRE(snap.current.has_value());
    REQUIRE(snap.temperature.has_value());
    REQUIRE(snap.fault.has_value());
}

// 不传 intervals 时默认只轮询角度（17ms），1s 后只有 angle 该有值。
TEST_CASE("Polling: default polls angle only", "[l30][polling]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({});
    std::this_thread::sleep_for(std::chrono::seconds{1});
    REQUIRE(hand.get_snapshot().angle.has_value());
}

// stop_polling() 调两次都应成功。
TEST_CASE("Polling: stop_polling is idempotent", "[l30][polling]") {
    auto &hand = test_util::l30_hand();
    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    std::this_thread::sleep_for(std::chrono::milliseconds{300});
    REQUIRE_NOTHROW(hand.stop_polling());
    REQUIRE_NOTHROW(hand.stop_polling());
}

// 隔 500ms 再拍一次，第二次角度时间戳应比第一次新（确实在刷新）。
TEST_CASE("Polling: snapshot updates over time", "[l30][polling]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    const auto first = hand.get_snapshot();
    REQUIRE(first.angle.has_value());
    const auto stamp = first.angle->timestamp; // NOLINT(bugprone-unchecked-optional-access)

    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    const auto second = hand.get_snapshot();
    REQUIRE(second.angle.has_value());
    REQUIRE(second.angle->timestamp > // NOLINT(bugprone-unchecked-optional-access)
            stamp);
}

// 轮询后的角度不只是有值，还应落在放宽后的合理区间，温度也应是合理的机身温度。
TEST_CASE("Polling: get_snapshot reflects polled data", "[l30][polling]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({
        {SensorSource::kAngle, std::chrono::milliseconds{50}},
        {SensorSource::kTemperature, std::chrono::milliseconds{50}},
    });
    std::this_thread::sleep_for(std::chrono::seconds{1});

    const auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
    REQUIRE(snap.temperature.has_value());

    for (size_t i = 0; i < kJointCount; ++i) {
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(snap.angle->angles.values[i] >= test_util::kAngleReadLowerBound);
        REQUIRE(snap.angle->angles.values[i] <= test_util::kAngleReadUpperBound);
        REQUIRE(snap.temperature->temperatures[i] < 100);
        // NOLINTEND(bugprone-unchecked-optional-access)
    }
}

// 非正的轮询间隔应抛 invalid_argument，且不会留下跑着的线程。
TEST_CASE("Polling: a non-positive interval is rejected", "[l30][polling]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    REQUIRE_THROWS_AS(hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{0}}}),
                      linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(hand.stop_polling());
}

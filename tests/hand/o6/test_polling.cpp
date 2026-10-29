#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <thread>

#include <linkerbot/hand/o6/events.hpp>

#include "hand/o6/o6_fixture.hpp"

using namespace linkerbot::hand::o6;

// 真机轮询。start_polling 后台按间隔发读请求，结果进 snapshot。

// 只轮询角度，snapshot.angle 应有值。
TEST_CASE("Polling: single source populates snapshot", "[o6][polling]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
}

// 只轮询点阵力，snapshot.force_sensor 应有值。
TEST_CASE("Polling: force sensor source populates snapshot", "[o6][polling]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto snap = hand.get_snapshot();
    REQUIRE(snap.force_sensor.has_value());
}

// 同时订角度/扭矩/速度/温度，四份 snapshot 都有值。
TEST_CASE("Polling: multiple sources populate snapshots", "[o6][polling]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle, SensorSource::kCurrent, SensorSource::kSpeed, SensorSource::kTemperature},
                       std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::milliseconds{1000});

    auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
    REQUIRE(snap.current.has_value());
    REQUIRE(snap.speed.has_value());
    REQUIRE(snap.temperature.has_value());
}

// 隔一段时间再取 snapshot，角度时间戳应前进。
TEST_CASE("Polling: snapshot updates over time", "[o6][polling]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::milliseconds{300});

    auto snap1 = hand.get_snapshot();
    REQUIRE(snap1.angle.has_value());
    auto ts1 = snap1.angle->timestamp; // NOLINT(bugprone-unchecked-optional-access)

    std::this_thread::sleep_for(std::chrono::milliseconds{300});

    auto snap2 = hand.get_snapshot();
    REQUIRE(snap2.angle.has_value());
    auto ts2 = snap2.angle->timestamp; // NOLINT(bugprone-unchecked-optional-access)

    REQUIRE(ts2 > ts1);
}

// stop_polling 调两次都不抛。
TEST_CASE("Polling: stop_polling is idempotent", "[o6][polling]") {
    auto &hand = test_util::o6_hand();

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    REQUIRE_NOTHROW(hand.stop_polling());
    REQUIRE_NOTHROW(hand.stop_polling());
}

// 默认 start_polling() 覆盖全部源，七份 snapshot 都有值。
TEST_CASE("Polling: polling all sources (default)", "[o6][polling]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling();
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
    REQUIRE(snap.current.has_value());
    REQUIRE(snap.speed.has_value());
    REQUIRE(snap.acceleration.has_value());
    REQUIRE(snap.temperature.has_value());
    REQUIRE(snap.fault.has_value());
    REQUIRE(snap.force_sensor.has_value());
}

// 轮询后 snapshot 里的角度值都在 [0, 100]。
TEST_CASE("Polling: get_snapshot reflects polled data", "[o6][polling]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle, SensorSource::kCurrent}, std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
    REQUIRE(snap.current.has_value());

    auto arr = snap.angle
                   ->angle // NOLINT(bugprone-unchecked-optional-access)
                   .to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 100.0f);
    }
}

#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <thread>

#include <linkerbot/hand/l20/events.hpp>

#include "hand/l20/l20_fixture.hpp"

using namespace linkerbot::hand::l20;

// 真机轮询。start_polling 后台按间隔发读请求，结果进 snapshot。
// 空 sources 默认六类（角度/扭矩/速度/温度/故障/力传感器），同一间隔（默认 100ms）。

// 只轮询角度，500ms 后 snapshot.angle 应有值。
TEST_CASE("Polling: single source populates snapshot", "[l20][polling]") {
    auto &hand = test_util::l20_hand();
    const test_util::L20PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().angle.has_value());
}

// 同时轮询角度、扭矩、速度、温度，四份 snapshot 都应有值。
TEST_CASE("Polling: multiple sources populate snapshots", "[l20][polling]") {
    auto &hand = test_util::l20_hand();
    const test_util::L20PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle, SensorSource::kCurrent, SensorSource::kSpeed, SensorSource::kTemperature},
                       std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::milliseconds{1000});

    auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
    REQUIRE(snap.current.has_value());
    REQUIRE(snap.speed.has_value());
    REQUIRE(snap.temperature.has_value());
}

// stop_polling() 调两次都应成功。
TEST_CASE("Polling: stop_polling is idempotent", "[l20][polling]") {
    auto &hand = test_util::l20_hand();
    const test_util::L20PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    REQUIRE_NOTHROW(hand.stop_polling());
    REQUIRE_NOTHROW(hand.stop_polling());
}

// 只轮询力传感器；点阵更慢，等 2s 后 snapshot.force_sensor 应有值。
TEST_CASE("Polling: force sensor source populates snapshot", "[l20][polling]") {
    auto &hand = test_util::l20_hand();
    const test_util::L20PollingGuard guard(hand);

    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::seconds{2});

    auto snap = hand.get_snapshot();
    REQUIRE(snap.force_sensor.has_value());
}

// 隔 300ms 再拍一次，第二次角度时间戳应比第一次新（确实在刷新）。
TEST_CASE("Polling: snapshot updates over time", "[l20][polling]") {
    auto &hand = test_util::l20_hand();
    const test_util::L20PollingGuard guard(hand);

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

// 停轮询后角度时间戳应冻结（整机 Snapshot.timestamp 是采样时刻，不能用来判断）。
TEST_CASE("Polling: snapshot frozen after stop", "[l20][polling]") {
    auto &hand = test_util::l20_hand();
    const test_util::L20PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    hand.stop_polling();

    auto snap1 = hand.get_snapshot();
    REQUIRE(snap1.angle.has_value());
    auto ts1 = snap1.angle->timestamp; // NOLINT(bugprone-unchecked-optional-access)

    std::this_thread::sleep_for(std::chrono::milliseconds{300});

    auto snap2 = hand.get_snapshot();
    REQUIRE(snap2.angle.has_value());
    auto ts2 = snap2.angle->timestamp; // NOLINT(bugprone-unchecked-optional-access)

    REQUIRE(ts2 == ts1);
}

// 不传 sources 时默认轮询全部六类，3s 后 snapshot 都应有值。
TEST_CASE("Polling: polling all sources (default)", "[l20][polling]") {
    auto &hand = test_util::l20_hand();
    const test_util::L20PollingGuard guard(hand);

    hand.start_polling();
    std::this_thread::sleep_for(std::chrono::seconds{3});

    auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
    REQUIRE(snap.current.has_value());
    REQUIRE(snap.speed.has_value());
    REQUIRE(snap.temperature.has_value());
    REQUIRE(snap.fault.has_value());
    REQUIRE(snap.force_sensor.has_value());
}

// 轮询后的 snapshot 不只是有值，十六个角度还应在 [0, 100]。
TEST_CASE("Polling: get_snapshot reflects polled data", "[l20][polling]") {
    auto &hand = test_util::l20_hand();
    const test_util::L20PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle, SensorSource::kCurrent}, std::chrono::milliseconds{100});
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    auto snap = hand.get_snapshot();
    REQUIRE(snap.angle.has_value());
    REQUIRE(snap.current.has_value());

    auto arr = test_support::require_value(snap.angle).angle.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 100.0f);
    }
}

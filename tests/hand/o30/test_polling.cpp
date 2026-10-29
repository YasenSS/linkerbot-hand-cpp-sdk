#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <stdexcept>
#include <thread>

#include <linkerbot/hand/o30/events.hpp>

#include "hand/o30/o30_fixture.hpp"

using namespace linkerbot::hand::o30;

// 真机轮询。O30 没有设备侧周期上报，全靠主机阻塞读。

// 只轮询角度，1s 后 snapshot.angle 应有值。
TEST_CASE("Polling: single source populates snapshot", "[o30][polling]") {
    auto &hand = test_util::o30_hand();
    const test_util::O30PollingGuard guard(hand);

    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    std::this_thread::sleep_for(std::chrono::seconds{1});
    REQUIRE(hand.get_snapshot().angle.has_value());
}

// 不传 intervals 时默认只轮询角度。
TEST_CASE("Polling: default polls angle only", "[o30][polling]") {
    auto &hand = test_util::o30_hand();
    const test_util::O30PollingGuard guard(hand);

    hand.start_polling({});
    std::this_thread::sleep_for(std::chrono::seconds{1});
    REQUIRE(hand.get_snapshot().angle.has_value());
}

// stop_polling() 调两次都应成功。
TEST_CASE("Polling: stop_polling is idempotent", "[o30][polling]") {
    auto &hand = test_util::o30_hand();
    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    std::this_thread::sleep_for(std::chrono::milliseconds{300});
    REQUIRE_NOTHROW(hand.stop_polling());
    REQUIRE_NOTHROW(hand.stop_polling());
}

// 间隔 ≤0 应抛 invalid_argument。
TEST_CASE("Polling: rejects a non-positive interval", "[o30][polling]") {
    auto &hand = test_util::o30_hand();
    REQUIRE_THROWS_AS(hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{0}}}),
                      linkerbot::hand::ValidationError);
}

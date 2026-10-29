#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>

#include <linkerbot/hand/l30/events.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/overloaded.hpp>

#include "hand/l30/l30_fixture.hpp"

using namespace linkerbot;
using namespace linkerbot::hand::l30;

// 真机 stream：事件既可以来自 start_polling 的主机侧读，
// 也可以来自设备侧的周期报告；两条路都进同一个队列。

static std::vector<SensorEvent> collect_events(L30 &hand, std::chrono::milliseconds duration) {
    std::vector<SensorEvent> collected;
    auto &queue = hand.stream();
    std::thread collector([&] {
        for (const auto &event : queue) {
            collected.push_back(event);
        }
    });
    std::this_thread::sleep_for(duration);
    hand.stop_stream();
    collector.join();
    return collected;
}

// 只轮询角度 2s，队列里应能收到事件。
TEST_CASE("Streaming: receives events", "[l30][streaming]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    const auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());
}

// 只开角度源时，收到的全应是 AngleEvent。
TEST_CASE("Streaming: event types match sources", "[l30][streaming]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    const auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());
    for (const auto &event : events) {
        REQUIRE(std::holds_alternative<AngleEvent>(event));
    }
}

// 已经 stream 时再调 stream() 应抛 logic_error。
TEST_CASE("Streaming: stream twice throws", "[l30][streaming]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    static_cast<void>(hand.stream());
    REQUIRE_THROWS_AS(hand.stream(), linkerbot::hand::LifecycleError);
    hand.stop_stream();
}

// 同时轮询角度和温度，两种事件都应出现。
TEST_CASE("Streaming: multiple event types", "[l30][streaming]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({
        {SensorSource::kAngle, std::chrono::milliseconds{50}},
        {SensorSource::kTemperature, std::chrono::milliseconds{50}},
    });
    const auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());

    bool has_angle = false;
    bool has_temperature = false;
    for (const auto &event : events) {
        has_angle = has_angle || std::holds_alternative<AngleEvent>(event);
        has_temperature = has_temperature || std::holds_alternative<TemperatureEvent>(event);
    }
    REQUIRE(has_angle);
    REQUIRE(has_temperature);
}

// std::visit + Overloaded 能走到每条事件。
TEST_CASE("Streaming: variant visitation", "[l30][streaming]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({
        {SensorSource::kAngle, std::chrono::milliseconds{50}},
        {SensorSource::kCurrent, std::chrono::milliseconds{50}},
    });
    const auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());

    size_t visited = 0;
    for (const auto &event : events) {
        std::visit(Overloaded{
                       [&](const AngleEvent &) { ++visited; },
                       [&](const CurrentEvent &) { ++visited; },
                       [&](const auto &) { ++visited; },
                   },
                   event);
    }
    REQUIRE(visited == events.size());
}

// 角度事件里的十七个百分比应在放宽后的合理区间内。
TEST_CASE("Streaming: event has valid data", "[l30][streaming]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    const auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());
    REQUIRE(std::holds_alternative<AngleEvent>(events.front()));

    const auto &angles = std::get<AngleEvent>(events.front()).data.angles;
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(angles.values[i] >= test_util::kAngleReadLowerBound);
        REQUIRE(angles.values[i] <= test_util::kAngleReadUpperBound);
    }
}

// 设备侧周期报告也会推事件：不开 start_polling，只靠构造时默认使能的
// 角度上报，2s 内也应收到 AngleEvent。
TEST_CASE("Streaming: device periodic reports feed the queue", "[l30][streaming]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_periodic_reports();
    const auto events = collect_events(hand, std::chrono::seconds{2});
    hand.stop_periodic_reports();

    REQUIRE_FALSE(events.empty());
    bool has_angle = false;
    for (const auto &event : events) {
        has_angle = has_angle || std::holds_alternative<AngleEvent>(event);
    }
    REQUIRE(has_angle);
}

// stop_stream 不抛；未 stream 时再 stop 也安全。
TEST_CASE("Streaming: stop_stream clean", "[l30][streaming]") {
    auto &hand = test_util::l30_hand();
    const test_util::L30PollingGuard guard(hand);

    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    static_cast<void>(hand.stream());
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    REQUIRE_NOTHROW(hand.stop_stream());
    REQUIRE_NOTHROW(hand.stop_stream());
}

#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>

#include <linkerbot/hand/o6/events.hpp>
#include <linkerbot/hand/o6/o6.hpp>
#include <linkerbot/overloaded.hpp>

#include "hand/o6/o6_fixture.hpp"

using namespace linkerbot;
using namespace linkerbot::hand::o6;

// 开 stream 收 duration 时长的事件，再 stop_stream。
static std::vector<SensorEvent> collect_events(O6 &hand, std::chrono::milliseconds duration) {
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

// 真机 stream：先 start_polling，事件从队列里出来。

// 只轮询角度 2s，队列里应有事件。
TEST_CASE("Streaming: receives events", "[o6][streaming]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});

    auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());
}

// 只订角度源时，事件全是 AngleEvent。
TEST_CASE("Streaming: event types match sources", "[o6][streaming]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});

    auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());

    for (const auto &event : events) {
        REQUIRE(std::holds_alternative<AngleEvent>(event));
    }
}

// 同时订角度和电流，两种事件都要出现。
TEST_CASE("Streaming: multiple event types", "[o6][streaming]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle, SensorSource::kCurrent}, std::chrono::milliseconds{100});

    auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());

    bool has_angle = false;
    bool has_current = false;
    for (const auto &event : events) {
        if (std::holds_alternative<AngleEvent>(event)) {
            has_angle = true;
        }
        if (std::holds_alternative<CurrentEvent>(event)) {
            has_current = true;
        }
    }
    REQUIRE(has_angle);
    REQUIRE(has_current);
}

// 首条 AngleEvent 六个角度都在 [0, 100]。
TEST_CASE("Streaming: event has valid data", "[o6][streaming]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});

    auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());

    const auto &event = events.front();
    REQUIRE(std::holds_alternative<AngleEvent>(event));

    const auto &angle_event = std::get<AngleEvent>(event);
    auto arr = angle_event.data.angle.to_array();
    for (size_t i = 0; i < Angle::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 100.0f);
    }
}

// stop_stream 调两次都不抛。
TEST_CASE("Streaming: stop_stream clean", "[o6][streaming]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});

    auto &queue = hand.stream();
    (void)queue;
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    REQUIRE_NOTHROW(hand.stop_stream());
    REQUIRE_NOTHROW(hand.stop_stream());
}

// 未 stop 再调 stream() 应抛 logic_error。
TEST_CASE("Streaming: stream twice throws", "[o6][streaming]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    (void)hand.stream();
    REQUIRE_THROWS_AS(hand.stream(), linkerbot::hand::LifecycleError);
    hand.stop_stream();
}

// std::visit 能覆盖队列里每条事件。
TEST_CASE("Streaming: variant visitation", "[o6][streaming]") {
    auto &hand = test_util::o6_hand();
    const test_util::O6PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle, SensorSource::kCurrent}, std::chrono::milliseconds{100});

    auto events = collect_events(hand, std::chrono::seconds{2});
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

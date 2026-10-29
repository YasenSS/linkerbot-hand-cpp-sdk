#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>

#include <linkerbot/hand/l6/events.hpp>
#include <linkerbot/hand/l6/l6.hpp>
#include <linkerbot/overloaded.hpp>

#include "hand/l6/l6_fixture.hpp"

using namespace linkerbot;
using namespace linkerbot::hand::l6;

// 收 duration 时间内的 stream 事件，然后 stop_stream 结束队列。
static std::vector<SensorEvent> collect_events(L6 &hand, std::chrono::milliseconds duration) {
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

// 真机 stream：先 start_polling，事件从队列里出来。PollingGuard 结束时停 stream/轮询。

// 只轮询角度 2s，队列里应能收到事件。
TEST_CASE("Streaming: receives events", "[l6][streaming]") {
    auto &hand = test_util::l6_hand();
    const test_util::PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});

    auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());
}

// 只开角度源时，收到的全应是 AngleEvent。
TEST_CASE("Streaming: event types match sources", "[l6][streaming]") {
    auto &hand = test_util::l6_hand();
    const test_util::PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});

    auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());

    for (const auto &event : events) {
        REQUIRE(std::holds_alternative<AngleEvent>(event));
    }
}

// 同时轮询角度和扭矩，两种事件都应出现。
TEST_CASE("Streaming: multiple event types", "[l6][streaming]") {
    auto &hand = test_util::l6_hand();
    const test_util::PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle, SensorSource::kTorque}, std::chrono::milliseconds{100});

    auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());

    bool has_angle = false;
    bool has_torque = false;
    for (const auto &event : events) {
        if (std::holds_alternative<AngleEvent>(event)) {
            has_angle = true;
        }
        if (std::holds_alternative<TorqueEvent>(event)) {
            has_torque = true;
        }
    }
    REQUIRE(has_angle);
    REQUIRE(has_torque);
}

// 角度事件里的六个值应在 [0, 100]。
TEST_CASE("Streaming: event has valid data", "[l6][streaming]") {
    auto &hand = test_util::l6_hand();
    const test_util::PollingGuard guard(hand);

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

// stop_stream 不抛；未 stream 时再 stop 也安全。
TEST_CASE("Streaming: stop_stream clean", "[l6][streaming]") {
    auto &hand = test_util::l6_hand();
    const test_util::PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle}, std::chrono::milliseconds{100});

    auto &queue = hand.stream();
    (void)queue;
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    REQUIRE_NOTHROW(hand.stop_stream());
    REQUIRE_NOTHROW(hand.stop_stream());
}

// 已经 stream 时再调 stream() 应抛 logic_error。
TEST_CASE("Streaming: stream twice throws", "[l6][streaming]") {
    auto &hand = test_util::l6_hand();
    const test_util::PollingGuard guard(hand);

    (void)hand.stream();
    REQUIRE_THROWS_AS(hand.stream(), linkerbot::hand::LifecycleError);
    hand.stop_stream();
}

// std::visit + Overloaded 能走到每条事件。
TEST_CASE("Streaming: variant visitation", "[l6][streaming]") {
    auto &hand = test_util::l6_hand();
    const test_util::PollingGuard guard(hand);

    hand.start_polling({SensorSource::kAngle, SensorSource::kTorque}, std::chrono::milliseconds{100});

    auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());

    size_t visited = 0;
    for (const auto &event : events) {
        std::visit(Overloaded{
                       [&](const AngleEvent &) { ++visited; },
                       [&](const TorqueEvent &) { ++visited; },
                       [&](const auto &) { ++visited; },
                   },
                   event);
    }
    REQUIRE(visited == events.size());
}

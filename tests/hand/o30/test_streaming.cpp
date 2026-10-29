#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>

#include <linkerbot/hand/o30/events.hpp>

#include "hand/o30/o30_fixture.hpp"

using namespace linkerbot;
using namespace linkerbot::hand::o30;

static std::vector<SensorEvent> collect_events(O30 &hand, std::chrono::milliseconds duration) {
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

// 真机 stream：事件来自主机侧轮询，没有设备周期上报。

// 只轮询角度 2s，队列里应能收到事件。
TEST_CASE("Streaming: receives events", "[o30][streaming]") {
    auto &hand = test_util::o30_hand();
    const test_util::O30PollingGuard guard(hand);

    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    const auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());
}

// 只开角度源时，收到的全应是 AngleEvent。
TEST_CASE("Streaming: event types match sources", "[o30][streaming]") {
    auto &hand = test_util::o30_hand();
    const test_util::O30PollingGuard guard(hand);

    hand.start_polling({{SensorSource::kAngle, std::chrono::milliseconds{50}}});
    const auto events = collect_events(hand, std::chrono::seconds{2});
    REQUIRE_FALSE(events.empty());
    for (const auto &event : events) {
        REQUIRE(std::holds_alternative<AngleEvent>(event));
    }
}

// 已经 stream 时再调 stream() 应抛 logic_error。
TEST_CASE("Streaming: stream twice throws", "[o30][streaming]") {
    auto &hand = test_util::o30_hand();
    const test_util::O30PollingGuard guard(hand);

    static_cast<void>(hand.stream());
    REQUIRE_THROWS_AS(hand.stream(), linkerbot::hand::LifecycleError);
    hand.stop_stream();
}

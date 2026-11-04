#include "hand/support/optional_value.hpp"
#include "hand_device_hardware_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <future>
#include <thread>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

namespace {
    HandSnapshot require_new_angles(HandDevice &hand, const HandSnapshot &before) {
        const auto next = hand.wait_for_snapshot(before, 2s);
        REQUIRE(next.connection_generation == before.connection_generation);
        REQUIRE(next.snapshot_sequence > before.snapshot_sequence);
        REQUIRE(next.angles.has_value());
        require_meta(test_support::require_value(next.angles).meta, next.connection_generation);
        REQUIRE(test_support::require_value(test_support::require_value(next.angles).meta.collected_at) >
                test_support::require_value(test_support::require_value(before.angles).meta.collected_at));
        return next;
    }
}

TEST_CASE("HandDevice hardware polling overloads and plans", "[hardware][device][polling][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    hand.open();
    auto before = hand.get_snapshot();
    hand.start_polling();
    REQUIRE(hand.get_health().polling_active);
    (void)require_new_angles(hand, before);
    hand.stop_polling();
    REQUIRE_FALSE(hand.get_health().polling_active);
    REQUIRE_NOTHROW(hand.stop_polling());
    before = hand.get_snapshot();
    hand.start_polling(PollingPlan{{TelemetrySemantic::kAngles, 50ms}});
    (void)require_new_angles(hand, before);
    hand.stop_polling();
    REQUIRE_THROWS_AS(hand.start_polling({{TelemetrySemantic::kAngles, 0ms}}), PollingPlanError);
    REQUIRE_THROWS_AS(hand.start_polling({{TelemetrySemantic::kAngles, -1ms}}), PollingPlanError);
    REQUIRE_THROWS_AS(hand.start_polling({{TelemetrySemantic::kAngles, 50ms}, {TelemetrySemantic::kAngles, 100ms}}),
                      PollingPlanError);
    // Fixed-underlying-type enum values are valid C++17, including unknown values and bit masks.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    REQUIRE_THROWS_AS(hand.start_polling({{static_cast<TelemetrySemantic>(255), 100ms}}), UnsupportedOperationError);
    REQUIRE_FALSE(hand.get_health().polling_active);
    REQUIRE_NOTHROW(hand.start_polling(PollingPlan{})); // Empty plan selects the default channel.
    hand.stop_polling();
}

TEST_CASE("HandDevice hardware snapshot wait timeout", "[hardware][device][polling][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    hand.open();
    const auto before = hand.get_snapshot();
    const auto started = std::chrono::steady_clock::now();
    const auto after = hand.wait_for_snapshot(before, 40ms);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE(elapsed < 1s);
    REQUIRE(after.connection_generation == before.connection_generation);
    REQUIRE(after.snapshot_sequence >= before.snapshot_sequence);
    if (after.snapshot_sequence == before.snapshot_sequence) {
        REQUIRE(elapsed >= 35ms);
    } else {
        INFO("Unsolicited device reports legitimately completed the wait before timeout");
    }
    REQUIRE_THROWS_AS(hand.wait_for_snapshot(after, 0ms), ValidationError);
    REQUIRE_THROWS_AS(hand.wait_for_snapshot(after, -1ms), ValidationError);
}

TEST_CASE("HandDevice hardware stream payload and bounded queue", "[hardware][device][polling][stream][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    hand.open();
    REQUIRE_THROWS_AS(hand.stream_handle(0), std::invalid_argument);
    auto queue = hand.stream_handle(1);
    hand.start_polling({{TelemetrySemantic::kAngles, 50ms}});
    auto consumer = std::async(std::launch::async, [queue] {
        // Streams also publish the open baseline (e.g. cached faults), even
        // when only the angle channel is currently being polled.
        while (auto event = queue->pop()) {
            if (event->semantic == TelemetrySemantic::kAngles) {
                return event;
            }
        }
        return std::optional<HandEvent>{};
    });
    const auto status = consumer.wait_for(2s);
    if (status != std::future_status::ready) {
        hand.stop_stream();
    }
    REQUIRE(status == std::future_status::ready);
    const auto event = consumer.get();
    REQUIRE(event.has_value());
    REQUIRE(test_support::require_value(event).semantic == TelemetrySemantic::kAngles);
    REQUIRE(std::holds_alternative<AngleSample>(test_support::require_value(event).sample));
    const auto angles = std::get<AngleSample>(test_support::require_value(event).sample);
    require_meta(angles.meta, hand.get_health().connection_generation);
    REQUIRE(angles.raw.size() == hand.get_capabilities().joints.size());
    const auto drops = queue->drop_count();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (queue->drop_count() == drops && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(20ms);
    }
    REQUIRE(queue->drop_count() > drops);
    auto replacement = hand.stream_handle(8);
    REQUIRE(queue->closed());
    hand.stop_stream();
    REQUIRE(replacement->closed());
    REQUIRE_NOTHROW(hand.stop_stream());
    size_t retained = 0;
    while (queue->pop()) {
        ++retained;
    }
    REQUIRE(retained <= 1);
    hand.stop_polling();
    REQUIRE_FALSE(hand.get_health().polling_active);
}

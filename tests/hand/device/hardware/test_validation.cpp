#include "hand_device_api_catalog.hpp"
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

namespace {
    void require_closed_operations(HandDevice &hand) {
        REQUIRE_FALSE(hand.is_open());
        REQUIRE_FALSE(hand.get_health().connected());
        REQUIRE_FALSE(hand.get_identity().has_value());
        REQUIRE_FALSE(hand.get_verified_capabilities().has_value());
        REQUIRE(hand.get_capabilities().validate());
        REQUIRE_THROWS_AS(hand.set_angles({}), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.set_percent_angles({}), DeviceNotReadyError);
        for (const auto &api : parameter_apis) {
            INFO(api.name);
            REQUIRE_THROWS_AS((hand.*api.set)({}), DeviceNotReadyError);
        }
        for (const auto &api : operation_apis) {
            INFO(api.name);
            REQUIRE_THROWS_AS((hand.*api.call)(), DeviceNotReadyError);
        }
        for (const auto &api : vector_apis) {
            INFO(api.name);
            REQUIRE_THROWS_AS(api.read(hand, 500ms), DeviceNotReadyError);
        }
        REQUIRE_THROWS_AS(hand.get_angles(500ms), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.get_faults(500ms), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.get_force(500ms), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.get_force({"fn"}, 500ms), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.get_normal_force_resultant(500ms), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.verify_identity(500ms), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.get_device_info(500ms), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.get_snapshot(), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.wait_for_snapshot({}, 20ms), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.start_polling(), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.start_polling({}), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.stop_polling(), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.recover(), DeviceNotReadyError);
        REQUIRE_THROWS_AS(hand.stream_handle(8), DeviceNotReadyError);
        REQUIRE_NOTHROW(hand.stop_stream()); // Idempotent cleanup is permitted while closed.
        REQUIRE_NOTHROW(hand.close());
    }
}

TEST_CASE("HandDevice hardware all operations require ready", "[hardware][device][validation][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    require_closed_operations(hand);
    hand.open();
    hand.close();
    require_closed_operations(hand);
}

TEST_CASE("HandDevice hardware numeric input validation", "[hardware][device][parameters][validation][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    hand.open();
    const auto caps = hand.get_capabilities();
    for (const auto &api : parameter_apis) {
        INFO(api.name);
        if (!caps.supports(api.semantic)) {
            REQUIRE_THROWS_AS((hand.*api.set)({}), UnsupportedOperationError);
            continue;
        }
        const auto *command = caps.command(api.semantic);
        REQUIRE(command != nullptr);
        const auto n = command->channels.size();
        REQUIRE(n > 0);
        std::vector<double> legal;
        for (size_t i = 0; i < n; ++i) {
            const auto *range = command->range_for(i);
            REQUIRE(range != nullptr);
            auto value = (range->minimum + range->maximum) / 2;
            if (range->integer_only) {
                value = std::round(value);
            }
            REQUIRE(range->contains(value));
            legal.push_back(value);
        }
        for (const auto size : {size_t{0}, n - 1, n + 1}) {
            REQUIRE_THROWS_AS((hand.*api.set)(std::vector<double>(size)), ValidationError);
        }
        for (size_t i = 0; i < n; ++i) {
            CAPTURE(i);
            const auto *range = command->range_for(i);
            for (const auto value :
                 {range->minimum - 1, range->maximum + 1, std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
                auto invalid = legal;
                invalid[i] = value;
                REQUIRE_THROWS_AS((hand.*api.set)(invalid), ValidationError);
            }
            if (range->integer_only) {
                auto invalid = legal;
                invalid[i] = std::floor(range->minimum) + 0.5;
                REQUIRE_THROWS_AS((hand.*api.set)(invalid), ValidationError);
            }
        }
    }
    for (const auto &api : operation_apis) {
        if (!caps.supports(api.semantic)) {
            INFO(api.name);
            REQUIRE_THROWS_AS((hand.*api.call)(), UnsupportedOperationError);
        }
    }
}

TEST_CASE("HandDevice hardware getter timeout validation", "[hardware][device][telemetry][validation][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    hand.open();
    const auto caps = hand.get_capabilities();
    for (const auto &api : vector_apis) {
        INFO(api.name);
        if (!caps.supports(api.semantic)) {
            REQUIRE_THROWS_AS(api.read(hand, 500ms), UnsupportedOperationError);
            continue;
        }
        REQUIRE_THROWS_AS(api.read(hand, 0ms), ValidationError);
        REQUIRE_THROWS_AS(api.read(hand, -1ms), ValidationError);
    }
    REQUIRE_THROWS_AS(hand.get_faults(0ms), ValidationError);
    REQUIRE_THROWS_AS(hand.get_faults(-1ms), ValidationError);
    REQUIRE_THROWS_AS(hand.get_force(0ms), ValidationError);
    REQUIRE_THROWS_AS(hand.get_force({"fn"}, -1ms), ValidationError);
    REQUIRE_THROWS_AS(hand.get_normal_force_resultant(0ms), ValidationError);
    REQUIRE_THROWS_AS(hand.verify_identity(0ms), ValidationError);
    REQUIRE_THROWS_AS(hand.get_device_info(-1ms), ValidationError);
    REQUIRE_THROWS_AS(hand.wait_for_snapshot(hand.get_snapshot(), 0ms), ValidationError);
}

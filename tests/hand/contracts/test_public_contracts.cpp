#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <linkerbot/hand/api/commands.hpp>
#include <linkerbot/hand/api/device.hpp>
#include <linkerbot/hand/api/readings.hpp>
#include <linkerbot/hand/capabilities.hpp>
#include <linkerbot/hand/conversion.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l30/l30.hpp>

#include "../../../src/hand/unified/detail/command_handle_access.hpp"

using namespace linkerbot::hand;

namespace {
#define ASSERT_REMOVED_METHOD(Name)                                                                                     \
    template <typename T, typename = void> struct Has_##Name : std::false_type {};                                       \
    template <typename T> struct Has_##Name<T, std::void_t<decltype(&T::Name)>> : std::true_type {};                      \
    static_assert(!Has_##Name<HandDevice>::value)

    ASSERT_REMOVED_METHOD(set_torques);
    ASSERT_REMOVED_METHOD(set_limit_compensations);
    ASSERT_REMOVED_METHOD(get_limit_compensations);
    ASSERT_REMOVED_METHOD(calibrate);
#undef ASSERT_REMOVED_METHOD

    template <typename T, typename = void> struct HasCalibrationManager : std::false_type {};
    template <typename T>
    struct HasCalibrationManager<T, std::void_t<decltype(&T::calibration)>> : std::true_type {};
    static_assert(!HasCalibrationManager<l30::L30>::value);
    static_assert(!Has_calibrate<OperationCapabilities>::value);

    // Removed enum values remain unused; surviving values must not shift.
    static_assert(static_cast<uint8_t>(CommandSemantic::kMotionTimes) == 9);
    static_assert(static_cast<uint8_t>(CommandSemantic::kStallHoldCurrents) == 12);
    static_assert(static_cast<uint8_t>(CommandFamily::kMotionTimeSetting) == 7);
    static_assert(static_cast<uint8_t>(CommandFamily::kDiscreteOperation) == 10);
    static_assert(static_cast<uint8_t>(DeviceOperation::kSaveParameters) == 5);
}

TEST_CASE("public contract: supported models are explicit and stable", "[contract]") {
    const auto &models = supported_models();
    REQUIRE(models.size() == 6);
    REQUIRE(models.front() == Model::kL6);
    REQUIRE(models.back() == Model::kO30);
    for (const auto model : models) {
        REQUIRE(model != Model::kUnknown);
    }
}

TEST_CASE("public contract: declared angle mappings convert both directions", "[contract][conversion]") {
    const auto &capabilities = declared_capabilities(Model::kL6);
    const std::vector<double> percent{0.0, 50.0, 100.0, 25.0, 75.0, 10.0};
    const auto raw = percent_to_raw_angles(percent, capabilities);

    REQUIRE(raw.size() == capabilities.joints.size());
    REQUIRE(raw[0] == 0.0);
    REQUIRE(raw[1] == 128.0);
    REQUIRE(raw[2] == 255.0);
    REQUIRE(validate_raw_angles(raw, capabilities) == raw);
    REQUIRE(raw_angles_within_declared_range(raw, capabilities));

    const auto round_trip = raw_angles_to_percent(raw, capabilities);
    REQUIRE(round_trip.size() == raw.size());
    REQUIRE(round_trip[0].value() == 0.0);
    REQUIRE(round_trip[2].value() == 100.0);

    const auto physical = raw_angles_to_physical_rad(raw, capabilities);
    REQUIRE(physical.size() == raw.size());
    for (const auto value : physical) {
        REQUIRE_FALSE(value.has_value());
    }
}

TEST_CASE("public contract: conversion validates shape, values and support", "[contract][conversion]") {
    const auto &capabilities = declared_capabilities(Model::kL6);
    REQUIRE_THROWS_AS(percent_to_raw_angles({0.0}, capabilities), ValidationError);
    REQUIRE_THROWS_AS(percent_to_raw_angles({0.0, 0.0, 0.0, 0.0, 0.0, 101.0}, capabilities), ValidationError);
    REQUIRE_THROWS_AS(
        validate_raw_angles({0.0, 0.0, 0.0, 0.0, 0.0, std::numeric_limits<double>::quiet_NaN()}, capabilities),
        ValidationError);

    auto unsupported = capabilities;
    unsupported.commands.clear();
    REQUIRE_THROWS_AS(percent_to_raw_angles({0.0, 0.0, 0.0, 0.0, 0.0, 0.0}, unsupported), UnsupportedOperationError);
}

TEST_CASE("public contract: feedback conversion preserves out-of-range evidence", "[contract][conversion]") {
    const auto &capabilities = declared_capabilities(Model::kL6);
    const std::vector<double> raw{-1.0, 0.0, 0.0, 0.0, 0.0, 256.0};
    const auto percent = raw_angles_to_percent(raw, capabilities);

    REQUIRE(percent.front().value() < 0.0);
    REQUIRE(percent.back().value() > 100.0);
    REQUIRE_FALSE(raw_angles_within_declared_range(raw, capabilities));
}

TEST_CASE("public contract: sample metadata has an explicit unavailable state", "[contract]") {
    const auto meta = SampleMeta::unavailable(7);
    REQUIRE(meta.sequence == 0);
    REQUIRE_FALSE(meta.collected_at.has_value());
    REQUIRE_FALSE(meta.age.has_value());
    REQUIRE_FALSE(meta.valid);
    REQUIRE(meta.stale);
    REQUIRE(meta.connection_generation == 7);
    REQUIRE(meta.origin == TelemetryValueOrigin::kNone);
}

TEST_CASE("public contract: command handle exposes shared waitable state", "[contract]") {
    CommandHandle handle{1, CommandFamily::kAngleTarget, AckSupport::kOptional};
    const auto copy = handle;

    REQUIRE(handle.command_id() == 1);
    REQUIRE(handle.family() == CommandFamily::kAngleTarget);
    REQUIRE(handle.stage() == CommandStage::kAccepted);
    REQUIRE(handle.ack_expectation() == AckExpectation::kRequired);
    REQUIRE(handle.history().size() == 1);
    REQUIRE_FALSE(handle.done());
    REQUIRE_FALSE(copy.wait_for(std::chrono::milliseconds{1}));
    REQUIRE_THROWS_AS(handle.wait_for(std::chrono::milliseconds{0}), ValidationError);
    REQUIRE_THROWS_AS(CommandHandle(0, CommandFamily::kAngleTarget, AckSupport::kUnsupported), ValidationError);
}

TEST_CASE("public contract: ACK capability and command expectation are distinct", "[contract][command]") {
    using command_detail::CommandHandleAccess;

    CommandHandle no_ack{2, CommandFamily::kAngleTarget, AckSupport::kOptional, AckExpectation::kNone};
    REQUIRE(no_ack.ack_support() == AckSupport::kOptional);
    REQUIRE(no_ack.ack_expectation() == AckExpectation::kNone);
    CommandHandleAccess::mark_dispatched(no_ack, 1);
    REQUIRE(no_ack.done());
    REQUIRE(no_ack.stage() == CommandStage::kDispatched);
    REQUIRE(no_ack.wait_for(std::chrono::milliseconds{1}));

    CommandHandle waits_for_ack{3, CommandFamily::kAngleTarget, AckSupport::kOptional, AckExpectation::kRequired};
    CommandHandleAccess::mark_dispatched(waits_for_ack, 2);
    REQUIRE_FALSE(waits_for_ack.done());
    CommandHandleAccess::mark_acknowledged(waits_for_ack);
    REQUIRE(waits_for_ack.done());
    REQUIRE(waits_for_ack.stage() == CommandStage::kAcknowledged);
    REQUIRE(waits_for_ack.sent_frames() == 2);

    REQUIRE_THROWS_AS(
        CommandHandle(4, CommandFamily::kAngleTarget, AckSupport::kUnsupported, AckExpectation::kRequired),
        ValidationError);
    REQUIRE_THROWS_AS(CommandHandle(5, CommandFamily::kAngleTarget, AckSupport::kRequired, AckExpectation::kNone),
                      ValidationError);
}

TEST_CASE("public contract: command transitions reject illegal states", "[contract][command]") {
    using command_detail::CommandHandleAccess;

    CommandHandle handle{6, CommandFamily::kAngleTarget, AckSupport::kRequired, AckExpectation::kRequired};
    REQUIRE_THROWS_AS(CommandHandleAccess::mark_acknowledged(handle), LifecycleError);
    REQUIRE_THROWS_AS(CommandHandleAccess::mark_dispatched(handle, 0), ValidationError);

    CommandHandleAccess::mark_dispatched(handle, 1);
    REQUIRE_THROWS_AS(CommandHandleAccess::mark_failed(handle, {}, 1), ValidationError);
    CommandHandleAccess::mark_failed(handle, std::make_exception_ptr(std::runtime_error("send failed")), 1);
    REQUIRE(handle.done());
    REQUIRE(handle.error() != nullptr);
    REQUIRE_THROWS_AS(CommandHandleAccess::mark_cancelled(handle), LifecycleError);
}

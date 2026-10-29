#include <catch2/catch_test_macros.hpp>

#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/health.hpp>
#include <linkerbot/hand/l20/l20.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l6/l6.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o6/o6.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/transport/cancellation.hpp>

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
    using linkerbot::hand::HandError;
    using linkerbot::hand::IdentityMismatchError;
    using linkerbot::hand::IdentityProtocolError;
    using linkerbot::hand::ProtocolError;
    using linkerbot::hand::runtime::RuntimeHealth;

    static_assert(linkerbot::hand::kErrorSchemaVersion == 1);
    static_assert(linkerbot::hand::kHealthSchemaVersion == 1);
    static_assert(std::is_base_of_v<std::runtime_error, HandError>);
    static_assert(std::is_base_of_v<ProtocolError, IdentityProtocolError>);
    static_assert(std::is_base_of_v<std::runtime_error, IdentityMismatchError>);
    static_assert(std::is_same_v<linkerbot::hand::runtime::CancelledError, linkerbot::transport::CancelledError>);

    template <class Model> using LegacyHealthResult = decltype(std::declval<const Model &>().runtime_health());

    static_assert(std::is_same_v<LegacyHealthResult<linkerbot::hand::l6::L6>, RuntimeHealth>);
    static_assert(std::is_same_v<LegacyHealthResult<linkerbot::hand::o6::O6>, RuntimeHealth>);
    static_assert(std::is_same_v<LegacyHealthResult<linkerbot::hand::l20::L20>, RuntimeHealth>);
    static_assert(std::is_same_v<LegacyHealthResult<linkerbot::hand::l20lite::L20lite>, RuntimeHealth>);
    static_assert(std::is_same_v<LegacyHealthResult<linkerbot::hand::l30::L30>, RuntimeHealth>);
    static_assert(std::is_same_v<LegacyHealthResult<linkerbot::hand::o30::O30>, RuntimeHealth>);
} // namespace

TEST_CASE("legacy named exceptions retain their catch contracts", "[compatibility][errors]") {
    bool caught_identity = false;
    try {
        throw IdentityProtocolError("invalid identity");
    } catch (const IdentityProtocolError &) {
        caught_identity = true;
    }
    REQUIRE(caught_identity);

    bool caught_runtime = false;
    try {
        throw linkerbot::hand::TimeoutError("timeout");
    } catch (const std::runtime_error &) {
        caught_runtime = true;
    }
    REQUIRE(caught_runtime);
}

TEST_CASE("legacy runtime health keeps its published fields", "[compatibility][health]") {
    RuntimeHealth health;
    health.state = linkerbot::hand::runtime::DeviceState::faulted;
    health.physical_generation = 2;
    health.device_generation = 3;
    health.completed = 4;
    health.timeouts = 5;
    health.cancelled = 6;
    health.protocol_errors = 7;
    health.late_frames = 8;
    health.event_drops = 9;
    health.duplicate_frames = 10;
    health.conflicting_duplicate_frames = 11;
    health.sent_frames = 12;
    health.in_flight = 13;
    health.polling_faulted = true;
    health.polling_error = "polling";
    health.last_error = "last";

    REQUIRE(health.device_generation == 3);
    REQUIRE(health.completed == 4);
    REQUIRE(health.sent_frames == 12);
    REQUIRE(health.in_flight == 13);
    REQUIRE(health.polling_faulted);
    REQUIRE(health.last_error == "last");
}

#include "hand/support/optional_value.hpp"
#include "hand_device_hardware_fixture.hpp"
#include "hand_device_transport_probe.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

TEST_CASE("HandDevice hardware repeated open close and recover", "[hardware][device][lifecycle][manual][read-only]") {
    if (env("HAND_DEVICE_ENABLE_LIFECYCLE_STRESS") != "1") {
        SKIP("set HAND_DEVICE_ENABLE_LIFECYCLE_STRESS=1 and attach Windows resource monitoring");
    }
    const auto cycles = parse_u32(env("HAND_DEVICE_LIFECYCLE_CYCLES")).value_or(100);
    REQUIRE(cycles >= 100);
    REQUIRE(cycles <= 10000);
    DeviceFixture fixture;
    auto &hand = fixture.device();
    std::optional<DeviceIdentity> identity;
    for (uint32_t cycle = 0; cycle < cycles; ++cycle) {
        CAPTURE(cycle);
        hand.open();
        if (!identity) {
            identity = test_support::require_value(hand.get_identity());
        }
        require_identity_equal(test_support::require_value(hand.get_identity()), *identity);
        hand.start_polling({{TelemetrySemantic::kAngles, 100ms}});
        require_meta(hand.get_angles(500ms).meta, hand.get_health().connection_generation);
        if (cycle % 10 == 0) {
            const auto generation = hand.get_health().connection_generation;
            hand.recover();
            REQUIRE(hand.get_health().connection_generation > generation);
            require_identity_equal(test_support::require_value(hand.get_identity()), *identity);
            REQUIRE(hand.get_health().polling_active);
            require_meta(hand.get_angles(500ms).meta, hand.get_health().connection_generation);
        }
        hand.close(); // close joins the active polling worker on every cycle.
        REQUIRE_FALSE(hand.is_open());
        REQUIRE_FALSE(hand.get_health().polling_active);
        REQUIRE(hand.get_health().pending_commands() == 0);
        std::cout << "lifecycle cycle=" << cycle + 1 << '/' << cycles << " closed\n" << std::flush;
    }
}

TEST_CASE("HandDevice hardware lifecycle and reopen", "[hardware][device][lifecycle][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    REQUIRE_FALSE(hand.is_open());
    REQUIRE_NOTHROW(hand.close());
    REQUIRE_NOTHROW(hand.stop_stream());
    hand.open();
    const auto original = test_support::require_value(hand.get_identity());
    const auto generation = hand.get_health().connection_generation;
    const auto opened = hand.get_snapshot();
    REQUIRE(hand.is_open());
    REQUIRE(hand.get_capabilities().validate());
    REQUIRE(hand.get_capabilities().model == fixture.config().model);
    REQUIRE(hand.get_health().connected());
    REQUIRE(hand.get_health().identity_verified);
    REQUIRE(hand.get_snapshot().angles.has_value());
    REQUIRE_NOTHROW(hand.open());
    REQUIRE(hand.get_health().connection_generation == generation);
    hand.start_polling();
    const auto events = hand.stream_handle(8);
    hand.close();
    REQUIRE(events->closed());
    REQUIRE_FALSE(hand.is_open());
    REQUIRE_FALSE(hand.get_health().connected());
    REQUIRE_FALSE(hand.get_health().polling_active);
    REQUIRE_FALSE(hand.get_identity().has_value());
    REQUIRE_FALSE(hand.get_verified_capabilities().has_value());
    REQUIRE(hand.get_health().pending_commands() == 0);
    REQUIRE_NOTHROW(hand.close());
    hand.open();
    require_identity_equal(test_support::require_value(hand.get_identity()), original);
    // close/open rebuilds a backend whose native counter may restart at 1.
    // Verify fresh identity and samples instead of requiring a global counter.
    const auto reopened = hand.get_snapshot();
    REQUIRE(test_support::require_value(test_support::require_value(reopened.angles).meta.collected_at) >
            test_support::require_value(test_support::require_value(opened.angles).meta.collected_at));
    REQUIRE_FALSE(hand.get_health().polling_active);
    require_meta(hand.get_angles(500ms).meta, hand.get_health().connection_generation);
}

TEST_CASE("HandDevice hardware recover identity baseline and polling", "[hardware][device][lifecycle][read-only]") {
    for (const auto path : {0, 1, 2}) {
        DYNAMIC_SECTION((path == 0   ? "hardware factory (O30 shared Session)"
                         : path == 1 ? "TransportFactory"
                                     : "shared Session")) {
            std::unique_ptr<DeviceFixture> native;
            std::unique_ptr<ProbedFixture> probed;
            if (path == 0) {
                native = std::make_unique<DeviceFixture>();
            } else {
                probed = std::make_unique<ProbedFixture>(path == 2);
            }
            auto &hand = native ? native->device() : *probed->hand;
            hand.open();
            const auto identity = test_support::require_value(hand.get_identity());
            hand.start_polling(PollingPlan{{TelemetrySemantic::kAngles, 50ms}});
            const auto before = hand.get_snapshot();
            hand.recover();
            REQUIRE(hand.is_open());
            REQUIRE(hand.get_health().connected());
            REQUIRE(hand.get_health().identity_verified);
            REQUIRE(hand.get_health().polling_active);
            REQUIRE(hand.get_health().pending_commands() == 0);
            require_identity_equal(test_support::require_value(hand.get_identity()), identity);
            REQUIRE(hand.get_verified_capabilities().has_value());
            const auto after = hand.get_snapshot();
            REQUIRE(after.connection_generation > before.connection_generation);
            REQUIRE(after.angles.has_value());
            require_meta(test_support::require_value(after.angles).meta, after.connection_generation);
            REQUIRE(after.faults.has_value());
            require_meta(test_support::require_value(after.faults).meta, after.connection_generation);
            REQUIRE(test_support::require_value(test_support::require_value(after.angles).meta.collected_at) >
                    test_support::require_value(test_support::require_value(before.angles).meta.collected_at));
            const auto next = hand.wait_for_snapshot(after, 2s);
            REQUIRE(next.angles.has_value());
            REQUIRE(next.snapshot_sequence > after.snapshot_sequence);
            REQUIRE(test_support::require_value(test_support::require_value(next.angles).meta.collected_at) >
                    test_support::require_value(test_support::require_value(after.angles).meta.collected_at));
            hand.stop_polling();
            REQUIRE_FALSE(hand.get_health().polling_active);
        }
    }
}

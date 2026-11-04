#include "hand/support/optional_value.hpp"
#include "hand_device_hardware_fixture.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

TEST_CASE("HandDevice hardware identity refresh and cache", "[hardware][device][identity][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    REQUIRE_FALSE(hand.get_identity().has_value());
    REQUIRE_FALSE(hand.get_verified_capabilities().has_value());
    hand.open();
    REQUIRE(hand.get_identity().has_value());
    const auto identity = test_support::require_value(hand.get_identity());
    std::cout << "Hardware identity: product=" << identity.product_code.value_or("unavailable")
              << ", serial=" << identity.serial_number.value_or("unavailable")
              << ", firmware=" << identity.firmware_version.value_or("unavailable")
              << ", hardware=" << identity.hardware_version.value_or("unavailable")
              << ", mechanical=" << identity.mechanical_version.value_or("unavailable") << '\n';
    REQUIRE(identity.model == fixture.config().model);
    if (fixture.config().side != HandSide::kUnknown) {
        REQUIRE(identity.side == fixture.config().side);
    }
    // HOP uses a fixed CAN endpoint, but does not report a device address.
    if (identity.model == Model::kO30) {
        REQUIRE_FALSE(identity.address.has_value());
    } else if (fixture.config().address) {
        REQUIRE(identity.address == fixture.config().address);
    }
    if (identity.model != Model::kO30 || fixture.config().side != HandSide::kUnknown) {
        REQUIRE(identity.side != HandSide::kUnknown);
    }
    REQUIRE(identity.product_code.has_value());
    REQUIRE_FALSE(test_support::require_value(identity.product_code).empty());
    // Protocols that do not expose identifiers must retain nullopt, not fabricate them.
    for (const auto &text :
         {identity.serial_number, identity.firmware_version, identity.hardware_version, identity.mechanical_version}) {
        if (text) {
            REQUIRE_FALSE(text->empty());
        }
    }
    if (identity.model != Model::kO30) {
        REQUIRE(identity.serial_number.has_value());
        REQUIRE(identity.firmware_version.has_value());
    }
    REQUIRE(hand.get_verified_capabilities().has_value());
    REQUIRE(test_support::require_value(hand.get_verified_capabilities()).validate());
    REQUIRE(test_support::require_value(hand.get_verified_capabilities()).model == identity.model);
    const auto refreshed = hand.verify_identity(2s);
    require_identity_equal(refreshed, identity);
    REQUIRE(refreshed.timestamp >= identity.timestamp);
    const auto info = hand.get_device_info(2s);
    require_identity_equal(info, identity);
    REQUIRE(info.timestamp >= refreshed.timestamp);
    require_identity_equal(test_support::require_value(hand.get_identity()), info);
    REQUIRE(hand.get_health().identity_verified);
    hand.close();
    REQUIRE_FALSE(hand.get_identity().has_value());
    REQUIRE_FALSE(hand.get_verified_capabilities().has_value());
    REQUIRE_FALSE(hand.get_health().identity_verified);
    REQUIRE_THROWS_AS(hand.get_device_info(2s), DeviceNotReadyError);
    hand.open();
    require_identity_equal(test_support::require_value(hand.get_identity()), identity);
}

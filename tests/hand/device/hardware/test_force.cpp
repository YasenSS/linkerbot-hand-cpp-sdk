#include "hand/support/optional_value.hpp"
#include "hand_device_hardware_fixture.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

namespace {
    void require_force(const ForceSample &sample, uint64_t generation) {
        require_meta(sample.meta, generation);
        REQUIRE_FALSE(sample.fields.empty());
        for (const auto &field : sample.fields) {
            INFO(field.name);
            require_meta(field.meta, generation);
            REQUIRE(field.raw.size() == field.sensor_names.size());
            REQUIRE(field.units.size() == field.raw.size());
            REQUIRE(field.encodings.size() == field.raw.size());
            REQUIRE(field.valid_values.size() == field.raw.size());
            bool present = false;
            for (size_t i = 0; i < field.raw.size(); ++i) {
                if (field.raw[i]) {
                    present = true;
                    REQUIRE_FALSE(test_support::require_value(field.raw[i]).empty());
                    for (const auto value : test_support::require_value(field.raw[i])) {
                        REQUIRE(std::isfinite(value));
                    }
                    if (field.valid_values[i]) {
                        REQUIRE(test_support::require_value(field.valid_values[i]).size() ==
                                test_support::require_value(field.raw[i]).size());
                    }
                }
            }
            REQUIRE(present);
            REQUIRE(sample.field(field.name) != nullptr);
            REQUIRE(sample.status(field.name) == "available");
        }
        for (const auto &sensor : sample.sensors) {
            REQUIRE(sensor.selected_sensor >= 1);
            REQUIRE(sensor.selected_sensor <= 5);
            if (sensor.available) {
                REQUIRE_FALSE(sensor.descriptor.empty());
                REQUIRE_FALSE(sensor.fields.empty());
                REQUIRE_FALSE(sensor.raw_data.empty());
            }
        }
    }

    std::vector<std::string> discover_fields(HandDevice &hand) {
        try {
            const auto sample = hand.get_force(2s);
            require_force(sample, hand.get_health().connection_generation);
        } catch (const UnsupportedOperationError &) {
            // Descriptor-based sensors may be absent. Confirm the observation,
            // rather than treating every UnsupportedOperationError as success.
            const auto observed = hand.get_capabilities();
            REQUIRE(observed.force_field("fn").supported == std::optional<bool>{false});
            REQUIRE(observed.force_field("fns").supported == std::optional<bool>{false});
        }
        std::vector<std::string> supported;
        for (const auto name : kForceFields) {
            const auto capability = hand.get_capabilities().force_field(name);
            if (capability.supported == false) {
                REQUIRE_THROWS_AS(hand.get_force({std::string{name}}, 2s), UnsupportedOperationError);
                continue;
            }
            const auto sample = hand.get_force({std::string{name}}, 2s);
            require_force(sample, hand.get_health().connection_generation);
            REQUIRE(sample.field(name) != nullptr);
            REQUIRE(sample.requested_fields == std::vector<std::string>{std::string{name}});
            supported.emplace_back(name);
        }
        return supported;
    }
}

TEST_CASE("HandDevice hardware force fields and selectors", "[hardware][device][force][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    hand.open();
    auto fields = discover_fields(hand);
    if (!fields.empty()) {
        const auto all = hand.get_force(fields, 2s);
        require_force(all, hand.get_health().connection_generation);
        REQUIRE(all.requested_fields == fields);
        for (const auto &name : fields) {
            REQUIRE(all.field(name) != nullptr);
        }
        const auto first = fields.front();
        const auto duplicate = hand.get_force({first, first}, 2s);
        REQUIRE(duplicate.requested_fields == std::vector<std::string>{first});
        require_force(duplicate, hand.get_health().connection_generation);
    }
    for (const auto &name : {std::string{}, std::string{"FN"}, std::string{"invalid"}}) {
        REQUIRE_THROWS_AS(hand.get_force({name}, 2s), ValidationError);
    }
    REQUIRE_THROWS_AS(hand.get_force({"fn", "invalid"}, 2s), ValidationError);
    REQUIRE_THROWS_AS(hand.get_force(0ms), ValidationError);
    REQUIRE_THROWS_AS(hand.get_force({"fn"}, -1ms), ValidationError);
}

TEST_CASE("HandDevice hardware normal force resultant", "[hardware][device][force][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    hand.open();
    const auto fields = discover_fields(hand);
    if (std::find(fields.begin(), fields.end(), "fns") == fields.end()) {
        REQUIRE_THROWS_AS(hand.get_normal_force_resultant(2s), UnsupportedOperationError);
        return;
    }
    const auto force = hand.get_force({"fns"}, 2s);
    const auto *field = force.field("fns");
    REQUIRE(field != nullptr);
    const auto scalar = std::all_of(field->raw.begin(), field->raw.end(),
                                    [](const auto &finger) { return finger && finger->size() == 1; });
    if (!scalar) {
        REQUIRE_THROWS_AS(hand.get_normal_force_resultant(2s), UnsupportedOperationError);
        return;
    }
    const auto result = hand.get_normal_force_resultant(2s);
    require_meta(result.meta, hand.get_health().connection_generation);
    REQUIRE(result.raw.size() == field->raw.size());
    REQUIRE(result.sensor_names == field->sensor_names);
    REQUIRE(result.units == field->units);
    for (const auto value : result.raw) {
        REQUIRE(std::isfinite(value));
    }
    const auto snapshot = hand.get_snapshot();
    REQUIRE(snapshot.normal_force_resultant.has_value());
    REQUIRE(test_support::require_value(snapshot.normal_force_resultant).raw == result.raw);
    REQUIRE_THROWS_AS(hand.get_normal_force_resultant(0ms), ValidationError);
}

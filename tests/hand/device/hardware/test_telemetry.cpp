#include "hand/support/optional_value.hpp"
#include "hand_device_api_catalog.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

TEST_CASE("HandDevice hardware vector telemetry semantics", "[hardware][device][telemetry][read-only]") {
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
        const auto *channel = caps.telemetry_channel(api.semantic);
        REQUIRE(channel != nullptr);
        RawVectorSample first;
        try {
            first = api.read(hand, 500ms);
        } catch (const DeviceNotReadyError &) {
            REQUIRE(
                (channel->read_mode == ReadMode::kCommandCache || channel->read_mode == ReadMode::kCommandResponse));
            REQUIRE(hand.is_open());
            REQUIRE(hand.get_health().identity_verified);
            std::cout << api.name << ": cache unavailable (DeviceNotReadyError)\n";
            continue;
        }
        if (!first.meta.valid &&
            (channel->read_mode == ReadMode::kCommandCache || channel->read_mode == ReadMode::kCommandResponse)) {
            // Opening a device must not invent an unwritten configuration.
            REQUIRE(first.raw.empty());
            REQUIRE_FALSE(first.meta.collected_at.has_value());
            REQUIRE(first.meta.stale);
            std::cout << api.name << ": no prior SDK command/response; cache unavailable\n";
            continue;
        }
        require_vector_sample(first, *channel, hand.get_health().connection_generation);
        const auto second = api.read(hand, 500ms);
        require_vector_sample(second, *channel, hand.get_health().connection_generation);
        if (channel->read_mode == ReadMode::kCommandCache || channel->read_mode == ReadMode::kCommandResponse) {
            REQUIRE(second.raw == first.raw);
            REQUIRE(second.meta.collected_at == first.meta.collected_at);
        } else {
            REQUIRE(*second.meta.collected_at > *first.meta.collected_at);
        }
    }
}

TEST_CASE("HandDevice hardware faults and snapshot projection", "[hardware][device][telemetry][read-only]") {
    DeviceFixture fixture;
    auto &hand = fixture.device();
    hand.open();
    const auto caps = hand.get_capabilities();
    const auto angles = hand.get_angles(500ms);
    const auto faults = hand.get_faults(500ms);
    const auto generation = hand.get_health().connection_generation;
    require_meta(angles.meta, generation);
    require_meta(faults.meta, generation);
    REQUIRE(faults.raw.size() == caps.joints.size());
    REQUIRE(faults.statuses.size() == faults.raw.size());
    for (size_t i = 0; i < faults.raw.size(); ++i) {
        REQUIRE(faults.statuses[i].raw == faults.raw[i]);
        REQUIRE(static_cast<uint8_t>(faults.statuses[i].known_bits | faults.statuses[i].reserved_bits) ==
                faults.raw[i]);
    }
    const auto snapshot = hand.get_snapshot();
    REQUIRE(snapshot.model == caps.model);
    REQUIRE(snapshot.side == test_support::require_value(hand.get_identity()).side);
    REQUIRE(snapshot.connection_generation == generation);
    REQUIRE(snapshot.device_info.has_value());
    REQUIRE(snapshot.angles.has_value());
    REQUIRE(snapshot.faults.has_value());
    REQUIRE(snapshot.joints.size() == caps.joints.size());
    REQUIRE(test_support::require_value(snapshot.angles).raw == angles.raw);
    REQUIRE(test_support::require_value(snapshot.faults).raw == faults.raw);
    require_meta(test_support::require_value(snapshot.angles).meta, generation);
    for (size_t i = 0; i < snapshot.joints.size(); ++i) {
        CAPTURE(i);
        REQUIRE(snapshot.joints[i].index == caps.joints[i].index);
        REQUIRE(snapshot.joints[i].name == caps.joints[i].name);
        REQUIRE(snapshot.joints[i].angle_raw == std::optional<double>{angles.raw[i]});
        REQUIRE(snapshot.joints[i].fault_raw == std::optional<uint8_t>{faults.raw[i]});
    }
}

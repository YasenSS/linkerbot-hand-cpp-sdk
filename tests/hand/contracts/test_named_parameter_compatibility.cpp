#include <catch2/catch_test_macros.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/l20lite/protocol_backend.hpp>
#include <linkerbot/hand/l6/protocol_backend.hpp>
#include <linkerbot/hand/o6/protocol_backend.hpp>
#include <linkerbot/hand/protocol/hop.hpp>

using namespace linkerbot::hand;

TEST_CASE("legacy discovery signatures retain distinct address and bitrate roles",
          "[comm][contracts][parameter-semantics]") {
    const std::string backend = "fake", name = "stable", native = "driver-channel";
    const DiscoveryChannel channel(backend, name, native, TransportKind::kCanFd, 500'000U, 2'000'000U, true, true,
                                   "timing");
    REQUIRE(channel.backend() == backend);
    REQUIRE(channel.name() == name);
    REQUIRE(channel.native_name() == native);
    REQUIRE(channel.nominal_bitrate() == 500'000U);
    REQUIRE(channel.data_bitrate() == 2'000'000U);
    REQUIRE(channel.configuration_id() == "timing");
    const DiscoveryChannel legacy(backend, name, TransportKind::kCanFd, 250'000U, 4'000'000U);
    REQUIRE(legacy.native_name() == name);
    REQUIRE(legacy.name() == name);
    REQUIRE(legacy.nominal_bitrate() == 250'000U);
    REQUIRE(legacy.data_bitrate() == 4'000'000U);
    const DiscoveryAddress address("extended", 0x1234U, 0x4321U, true, uint8_t{3}, uint8_t{5});
    REQUIRE(address.request_id() == 0x1234U);
    REQUIRE(address.response_id() == 0x4321U);
    REQUIRE(address.node_id() == 3);
    REQUIRE(address.host_id() == 5);
    const DiscoveryTransportSpec spec(
        backend, name, TransportKind::kCanFd, [] { return std::make_unique<linkerbot::can::FakeTransport>(); },
        125'000U, 1'000'000U, native, "custom");
    const auto key = spec.connection_key();
    REQUIRE(key.nominal_bitrate == 125'000U);
    REQUIRE(key.data_bitrate == 1'000'000U);
    REQUIRE(key.native_channel == native);
    REQUIRE(key.configuration_id == "custom");
    REQUIRE(spec.factory()() != nullptr);
}

TEST_CASE("legacy protocol function pointers retain selector and payload roles",
          "[comm][protocol][parameter-semantics]") {
    using Frame = linkerbot::can::Message (*)(uint32_t, uint8_t, const std::vector<uint8_t> &);
    const Frame legacy_frame = &l6::protocol::frame;
    const auto classic = legacy_frame(0x123U, 0x04, {0x21, 0x34});
    REQUIRE(classic.id == 0x123U);
    REQUIRE(classic.dlc == 3);
    REQUIRE(classic.data[0] == 0x04);
    REQUIRE(classic.data[1] == 0x21);
    REQUIRE(classic.data[2] == 0x34);
    using Hop = linkerbot::can::Message (*)(uint32_t, uint8_t, uint8_t, uint8_t, const std::vector<uint8_t> &, bool);
    const Hop legacy_hop = &protocol::hop::build_message;
    const auto hop = legacy_hop(0x234U, 3, 17, 1, {42}, true);
    REQUIRE(hop.id == 0x234U);
    REQUIRE(protocol::hop::mi_of(hop.data[0]) == 3);
    REQUIRE(protocol::hop::rts_of(hop.data[0]));
    REQUIRE(hop.data[1] == 17);
    const auto change = o6::protocol::write_config_set_id(0x28U, 2, 0x27);
    REQUIRE(change.frames.size() == 1);
    REQUIRE(change.frames.front().id == 0x28U);
    REQUIRE(change.frames.front().data[1] == 2);
    REQUIRE(change.frames.front().data[2] == 0x27);
}

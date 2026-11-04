#include "../../validation/fake_classic_hand.hpp"
#include "hand/l30/comm_helpers.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <linkerbot/hand/adapter_transport.hpp>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/l20/l20.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l6/l6.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o6/o6.hpp>
#include <optional>
#include <type_traits>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define CALL __stdcall
#else
#include <dlfcn.h>
#define CALL
#endif
namespace hand = linkerbot::hand;
namespace can = linkerbot::can;
// Guard every documented model-specific injection path in the Windows build.
using SessionPtr = std::shared_ptr<linkerbot::transport::Session>;
using TransportPtr = std::unique_ptr<can::CANTransport>;
static_assert(std::is_constructible_v<hand::l6::L6, hand::l6::Side, SessionPtr>);
static_assert(std::is_constructible_v<hand::l6::L6, hand::l6::Side, TransportPtr>);
static_assert(std::is_constructible_v<hand::o6::O6, hand::o6::Side, SessionPtr>);
static_assert(std::is_constructible_v<hand::o6::O6, hand::o6::Side, TransportPtr>);
static_assert(std::is_constructible_v<hand::l20::L20, hand::l20::Side, SessionPtr>);
static_assert(std::is_constructible_v<hand::l20::L20, hand::l20::Side, TransportPtr>);
static_assert(std::is_constructible_v<hand::l20lite::L20lite, hand::l20lite::Side, SessionPtr>);
static_assert(std::is_constructible_v<hand::l20lite::L20lite, hand::l20lite::Side, TransportPtr>);
static_assert(std::is_constructible_v<hand::l30::L30, SessionPtr, uint8_t, uint8_t, bool>);
static_assert(std::is_constructible_v<hand::l30::L30, TransportPtr, uint8_t, uint8_t, bool>);
static_assert(std::is_constructible_v<hand::o30::O30, SessionPtr>);
static_assert(std::is_constructible_v<hand::o30::O30, TransportPtr>);
namespace {
    struct Fixture {
        using Get = uint32_t(CALL *)(unsigned);
        using Set = uint32_t(CALL *)(unsigned, uint32_t);
        using Clear = uint32_t(CALL *)();
        using Reply = uint32_t(CALL *)(uint16_t, uint32_t, unsigned, unsigned, const void *);
        std::string path;
        std::shared_ptr<can::PcanApi> api;
        Get get;
        Set set;
        Clear clear;
        Reply reply;
        template <typename F> F symbol(const char *name) {
#if defined(_WIN32)
            return reinterpret_cast<F>(GetProcAddress(static_cast<HMODULE>(api->library_owner.get()), name));
#else
            return reinterpret_cast<F>(dlsym(api->library_owner.get(), name));
#endif
        }
        Fixture() {
            const auto value = std::getenv("LINKERBOT_TEST_PCAN_LIBRARY");
            if (!value) {
                throw std::runtime_error("Missing PCAN fixture path");
            }
            path = value;
            api = can::load_pcan_api(path);
            get = symbol<Get>("Test_Get");
            set = symbol<Set>("Test_Set");
            clear = symbol<Clear>("Test_ClearReplies");
            reply = symbol<Reply>("Test_AddReply");
            if (!get || !set || !clear || !reply) {
                throw std::runtime_error("Missing PCAN fixture controls");
            }
            for (unsigned i = 0; i < 32; ++i) {
                set(i, 0);
            }
            clear();
            set(14, 1);
        }
        ~Fixture() {
            set(11, 0);
            try {
                can::retry_pcan_cleanup(api);
            } catch (...) {
                INFO("PCAN fixture cleanup failed after clearing the injected close error");
                CHECK(false);
            }
        }
        void l6(const std::string &product = "LHL6-1-123-L") {
            clear();
            for (unsigned command = 0; command <= 255; ++command) {
                can::Message request;
                request.id = 0x28;
                request.dlc = 1;
                request.data[0] = uint8_t(command);
                if (command == 0xc1) {
                    request.dlc = 2;
                    request.data[1] = 1;
                }
                auto replies = validation::l6_reply(request);
                if (command == 0xc0) {
                    for (size_t chunk = 0; chunk < replies.size(); ++chunk) {
                        for (size_t i = 0; i < 6; ++i) {
                            replies[chunk].data[i + 2] =
                                chunk * 6 + i < product.size() ? uint8_t(product[chunk * 6 + i]) : 0;
                        }
                    }
                }
                for (const auto &response : replies) {
                    can::pcan_abi::ClassicMessage native{};
                    native.id = response.id;
                    native.length = response.dlc;
                    std::copy(response.data.begin(), response.data.begin() + 8, native.data);
                    REQUIRE(reply(0x51, request.id, command, 0, &native) == 0);
                }
            }
        }
        void add_fd(uint32_t id, const can::FakeTransport::Handler &handler) {
            can::Message request;
            request.id = id;
            constexpr unsigned sizes[]{0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
            for (const auto &response : handler(request)) {
                can::pcan_abi::FdMessage native{};
                native.id = response.id;
                native.type = 6;
                native.dlc =
                    uint8_t(std::lower_bound(std::begin(sizes), std::end(sizes), response.dlc) - std::begin(sizes));
                std::copy(response.data.begin(), response.data.end(), native.data);
                REQUIRE(reply(0x52, id, 256, 1, &native) == 0);
            }
        }
        void l30() {
            namespace p = hand::l30::protocol;
            const std::vector<uint8_t> info{0x13, 0, 0, 0, 0x7b, 1, 2, 3, 2, 3, 4, 3, 4, 5, 1, 0, 'B', 'A'};
            add_fd(l30_comm::request_id(p::kParentConfig, p::kSubDeviceInfo, p::kAccessRead),
                   l30_comm::payload_handler(info));
            const std::string product = "LHT30-06-123-L-B-3-A";
            add_fd(l30_comm::request_id(p::kParentConfig, p::kSubProductCode, p::kAccessRead),
                   l30_comm::payload_handler({product.begin(), product.end()}));
            std::array<int16_t, hand::l30::kJointCount> angles{};
            angles.fill(16);
            add_fd(l30_comm::query_id(p::kSubPosition), l30_comm::i16_handler(angles));
            add_fd(l30_comm::query_id(p::kSubFault), l30_comm::u8_handler(l30_comm::filled_u8(0)));
        }
        hand::DiscoveryConfig config(bool fd = false) const {
            hand::DiscoveryConfig result;
            result.response_timeout = std::chrono::milliseconds(20);
            result.inter_request_delay = std::chrono::milliseconds(0);
            result.total_timeout = std::chrono::seconds(3);
            result.pcan.library_path = std::filesystem::u8path(path);
            result.pcan.channels = {fd ? "PCAN_USBBUS2" : "PCAN_USBBUS1"};
            result.pcan.probe_protocols = {fd ? hand::TransportKind::kCanFd : hand::TransportKind::kCan};
            result.l30_node_ids = {1};
            result.o30_request_ids = {1};
            return result;
        }
    };
}
TEST_CASE("default PCAN Classic source completes discover create open read close", "[adapter_native]") {
    Fixture fixture;
    fixture.l6();
    auto config = fixture.config();
    std::optional<hand::DiscoveryCandidate> selected;
    {
        hand::PCANDiscoverySource source;
        CHECK(fixture.get(7) == 0);
        const auto report = hand::discover_hands(config, source);
        for (const auto &failure : report.failures()) {
            UNSCOPED_INFO(failure.code() << ": " << failure.message());
        }
        for (const auto &candidate : report.candidates()) {
            for (const auto &problem : candidate.problems()) {
                UNSCOPED_INFO(problem.code() << ": " << problem.message());
            }
        }
        INFO("candidates=" << report.candidates().size() << " classic init=" << fixture.get(0)
                           << " write=" << fixture.get(5) << " read=" << fixture.get(3)
                           << " last TX=" << fixture.get(20) << " len=" << fixture.get(21)
                           << " last RX=" << fixture.get(22) << " len=" << fixture.get(23));
        REQUIRE(report.verified_candidates().size() == 1);
        selected = report.require_one();
    }
    REQUIRE(selected);
    CHECK(selected->model() == hand::Model::kL6);
    CHECK(selected->side() == hand::HandSide::kLeft);
    CHECK(selected->transport().configuration_id().find(fixture.api->library_path) != std::string::npos);
    CHECK(selected->transport().connection_key().configuration_id == selected->channel().configuration_id());
    const auto opened = fixture.get(0);
    const auto closed = fixture.get(2);
    CHECK(opened == closed);
    config.pcan.library_path = "__missing__/PCANBasic.dll";
    config.pcan.nominal_bitrate = 500000;
    auto device = selected->create_hand_device();
    CHECK_FALSE(device->is_open());
    CHECK(fixture.get(0) == opened);
    device->open();
    REQUIRE(device->get_identity());
    CHECK(device->get_device_info().model == hand::Model::kL6);
    CHECK(device->get_angles().raw.size() == 6);
    device->close();
    CHECK(fixture.get(0) == opened + 1);
    CHECK(fixture.get(2) == closed + 1);
    fixture.l6("LHL6-1-999-L");
    auto replaced = selected->create_hand_device();
    CHECK_THROWS_AS(replaced->open(), hand::IdentityMismatchError);
    CHECK_FALSE(replaced->is_open());
}
TEST_CASE("default PCAN FD source preserves native channel timing and identity", "[adapter_native]") {
    Fixture fixture;
    fixture.l30();
    hand::PCANDiscoverySource source;
    auto config = fixture.config(true);
    const auto report = hand::discover_hands(config, source);
    REQUIRE(report.verified_candidates().size() == 1);
    const auto &candidate = report.require_one();
    CHECK(candidate.model() == hand::Model::kL30);
    CHECK(candidate.channel().native_name() == "PCAN_USBBUS2");
    CHECK(candidate.transport().configuration_id().find(config.pcan.fd_timing.bitrate_string(1000000, 5000000)) !=
          std::string::npos);
    CHECK(fixture.get(1) == fixture.get(2));
    const auto before = fixture.get(1);
    auto device = candidate.create_hand_device();
    CHECK_FALSE(device->is_open());
    CHECK(fixture.get(1) == before);
    REQUIRE_NOTHROW(device->open());
    CHECK(device->get_device_info().model == hand::Model::kL30);
    CHECK(device->get_angles().raw.size() == hand::l30::kJointCount);
    device->close();
    CHECK(fixture.get(1) == before + 1);
    CHECK(fixture.get(0) == 0);
}
TEST_CASE("manual adapter factories are lazy and use injected Windows backends", "[adapter_native]") {
    Fixture fixture;
    fixture.l6();
    can::PcanConfig config;
    config.channel = 0x51;
    config.library_path = fixture.path;
    auto factory = hand::make_pcan_transport_factory(config);
    CHECK(fixture.get(7) == 0);
    CHECK(fixture.get(0) == 0);
    auto settings = validation::config();
    settings.interface_name = "pcan:PCAN_USBBUS1";
    hand::HandDevice device(settings, factory);
    CHECK(fixture.get(0) == 0);
    REQUIRE_NOTHROW(device.open());
    CHECK(device.get_angles().raw.size() == 6);
    device.close();
    device.open();
    CHECK(device.get_device_info().serial_number == "123");
    device.close();
    CHECK(fixture.get(0) == 2);
    CHECK(fixture.get(2) == 2);
    can::VendorCanFdTransport::Config mini;
    mini.library_path = "__missing__/HCanbus.dll";
    auto missing = hand::make_minicanfd_transport_factory(mini);
    CHECK_THROWS_AS(missing(), can::TransportIOError);
}
TEST_CASE("PCAN enumeration and initialization costs stop new discovery work after budget", "[adapter_native]") {
    // Leave enough setup time on Windows runners with coarse timer resolution.
    // Ten delayed GetValue calls or a single delayed initialization exceed it.
    Fixture fixture;
    fixture.l6();
    hand::PCANDiscoverySource source;
    auto config = fixture.config();
    config.total_timeout = std::chrono::milliseconds(100);
    fixture.set(16, 20);
    const auto enumeration = hand::discover_hands(config, source);
    CHECK(enumeration.candidates().empty());
    CHECK(fixture.get(0) == 0);
    CHECK(std::any_of(enumeration.failures().begin(), enumeration.failures().end(),
                      [](const auto &f) { return f.code() == "timeout"; }));
    fixture.set(16, 0);
    fixture.set(17, 200);
    const auto opening = hand::discover_hands(config, source);
    CHECK(opening.candidates().empty());
    CHECK(fixture.get(0) == 1);
    CHECK(fixture.get(0) == fixture.get(2));
    CHECK(fixture.get(5) == 0);
}

TEST_CASE("default PCAN remains usable when another Windows backend is unavailable", "[adapter_native]") {
    Fixture fixture;
    fixture.l6();
    hand::CompositeDiscoverySource source({{"slcan", std::make_shared<hand::SLCANDiscoverySource>()},
                                           {"pcan", std::make_shared<hand::PCANDiscoverySource>()}});
    const auto report = hand::discover_hands(fixture.config(), source);
    REQUIRE(report.verified_candidates().size() == 1);
    CHECK(report.require_one().channel().backend() == "pcan");
    CHECK(std::any_of(report.failures().begin(), report.failures().end(), [](const auto &problem) {
        return problem.backend() == "slcan" && problem.code() == "allowlist_required";
    }));
}
TEST_CASE("default PCAN FD selection rejects Classic-only and unknown capability channels", "[adapter_native]") {
    Fixture fixture;
    hand::PCANDiscoverySource source;
    auto config = fixture.config(true);
    config.pcan.channels = {"PCAN_USBBUS1", "PCAN_USBBUS3"};
    const auto report = hand::discover_hands(config, source);
    CHECK(report.channels().empty());
    CHECK(report.candidates().empty());
    CHECK(fixture.get(0) == 0);
    CHECK(fixture.get(1) == 0);
    CHECK(std::any_of(report.failures().begin(), report.failures().end(),
                      [](const auto &problem) { return problem.stage() == "capability"; }));
    CHECK(std::any_of(report.failures().begin(), report.failures().end(),
                      [](const auto &problem) { return problem.code() == "unsupported_protocol"; }));
}

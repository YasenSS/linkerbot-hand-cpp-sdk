#include "hand/support/throw_as.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/adapter_transport.hpp>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using namespace linkerbot::hand;

namespace {
    using linkerbot::can::FakeTransport;
    using linkerbot::can::Message;

    [[nodiscard]] DiscoveryConfig config() {
        DiscoveryConfig result;
        result.response_timeout = std::chrono::milliseconds{20};
        result.send_timeout = std::chrono::milliseconds{100};
        result.inter_request_delay = std::chrono::milliseconds{0};
        result.total_timeout = std::chrono::seconds{2};
        result.l30_node_ids = {1};
        result.o30_request_ids = {1};
        result.pcan.probe_protocols = {TransportKind::kCanFd};
        result.slcan.probe_protocols = {TransportKind::kCanFd};
        return result;
    }

    void install_l30(FakeTransport *transport) {
        namespace protocol = linkerbot::hand::l30::protocol;
        const std::vector<uint8_t> info{0x13, 0x00, 0x00, 0x00, 0x7B, 1, 2, 3, 2, 3, 4, 3, 4, 5, 1, 0, 'B', 'A'};
        transport->on_request(
            l30_comm::request_id(protocol::kParentConfig, protocol::kSubDeviceInfo, protocol::kAccessRead),
            l30_comm::payload_handler(info));

        const std::string code = "LHT30-06-123-L-B-3-A";
        transport->on_request(
            l30_comm::request_id(protocol::kParentConfig, protocol::kSubProductCode, protocol::kAccessRead),
            l30_comm::payload_handler(std::vector<uint8_t>(code.begin(), code.end())));
    }

    [[nodiscard]] AdapterChannelEnumerator enumerator(std::string backend) {
        return [backend = std::move(backend)](const DiscoveryConfig &) {
            const auto native = backend == "pcan" ? "PCAN_USBBUS1" : backend == "slcan" ? "COM5" : "0:0";
            return DiscoveryEnumeration{{DiscoveryChannel{backend, backend + ':' + native, native,
                                                          TransportKind::kCanFd, 1'000'000U, 5'000'000U, false, true}}};
        };
    }

    [[nodiscard]] AdapterTransportFactory transport_factory(const std::shared_ptr<std::atomic<int>> &created) {
        return [created](const std::string &, TransportKind protocol) {
            if (protocol != TransportKind::kCanFd) {
                throw std::runtime_error("expected CAN FD probe");
            }
            ++*created;
            auto transport = std::make_unique<FakeTransport>();
            install_l30(transport.get());
            return std::unique_ptr<linkerbot::can::CANTransport>(std::move(transport));
        };
    }

    template <typename Source> [[nodiscard]] DiscoveryReport discover_with_source(const std::string &backend) {
        auto created = std::make_shared<std::atomic<int>>(0);
        Source source{enumerator(backend), transport_factory(created)};
        auto report = discover_hands(config(), source);
        REQUIRE(*created == 1);
        return report;
    }

    template <typename Source> void require_empty_enumeration(const std::string &backend) {
        int transports = 0;
        Source source{[](const DiscoveryConfig &) { return DiscoveryEnumeration{}; },
                      [&transports](const std::string &, TransportKind) {
                          ++transports;
                          return std::unique_ptr<linkerbot::can::CANTransport>(std::make_unique<FakeTransport>());
                      }};
        const auto report = discover_hands(config(), source);
        INFO("backend=" << backend);
        REQUIRE(transports == 0);
        REQUIRE(report.channels().empty());
        REQUIRE(report.candidates().empty());
        REQUIRE(report.failures().empty());
    }

    template <typename Source> void require_enumeration_failure(const std::string &backend) {
        int transports = 0;
        Source source{[](const DiscoveryConfig &) {
                          return test_util::throw_as<DiscoveryEnumeration>(std::runtime_error("enumeration failed"));
                      },
                      [&transports](const std::string &, TransportKind) {
                          ++transports;
                          return std::unique_ptr<linkerbot::can::CANTransport>(std::make_unique<FakeTransport>());
                      }};
        const auto report = discover_hands(config(), source);
        INFO("backend=" << backend);
        REQUIRE(transports == 0);
        REQUIRE(report.channels().empty());
        REQUIRE(report.candidates().empty());
        REQUIRE(report.failures().size() == 1);
        REQUIRE(report.failures().front().backend() == backend);
        REQUIRE(report.failures().front().stage() == "enumerate");
    }

    template <typename Source> void require_failed_channel_isolation(const std::string &backend) {
        AdapterChannelEnumerator enumerate = [backend](const DiscoveryConfig &) {
            return DiscoveryEnumeration{{DiscoveryChannel{backend, backend + ":bad", "bad", TransportKind::kCanFd,
                                                          1'000'000U, 5'000'000U, false, true},
                                         DiscoveryChannel{backend, backend + ":good", "good", TransportKind::kCanFd,
                                                          1'000'000U, 5'000'000U, false, true}}};
        };
        AdapterTransportFactory factory = [](const std::string &channel, TransportKind) {
            if (channel == "bad") {
                throw linkerbot::can::TransportIOError("adapter is occupied", {}, 19);
            }
            auto transport = std::make_unique<FakeTransport>();
            install_l30(transport.get());
            return std::unique_ptr<linkerbot::can::CANTransport>(std::move(transport));
        };
        Source source{std::move(enumerate), std::move(factory)};

        const auto report = discover_hands(config(), source);
        INFO("backend=" << backend);
        REQUIRE(report.verified_candidates().size() == 1);
        REQUIRE(report.require_one().channel().native_name() == "good");
        REQUIRE(report.failures().size() == 1);
        REQUIRE(report.failures().front().backend() == backend);
        REQUIRE(report.failures().front().native_channel() == "bad");
        REQUIRE(report.failures().front().native_status() == 19);
    }
} // namespace

TEST_CASE("adapter discovery sources share the identity probe engine", "[discovery][adapter]") {
    const auto pcan = discover_with_source<PCANDiscoverySource>("pcan");
    const auto slcan = discover_with_source<SLCANDiscoverySource>("slcan");
    const auto minican = discover_with_source<MiniCANFDDiscoverySource>("minicanfd");

    for (const auto *report : {&pcan, &slcan, &minican}) {
        REQUIRE(report->verified_candidates().size() == 1);
        const auto &candidate = report->require_one();
        REQUIRE(candidate.model() == Model::kL30);
        REQUIRE(candidate.side() == HandSide::kLeft);
        REQUIRE(candidate.address().node_id() == std::optional<uint8_t>{1});
        REQUIRE(candidate.transport().connection_key().native_channel == candidate.channel().native_name());
    }
    REQUIRE(pcan.require_one().identity().product_code == slcan.require_one().identity().product_code);
    REQUIRE(slcan.require_one().identity().product_code == minican.require_one().identity().product_code);
}

TEST_CASE("adapter candidate factory outlives its discovery source", "[discovery][adapter]") {
    auto created = std::make_shared<std::atomic<int>>(0);
    std::optional<DiscoveryCandidate> candidate;
    {
        SLCANDiscoverySource source{enumerator("slcan"), transport_factory(created)};
        auto report = discover_hands(config(), source);
        candidate.emplace(report.require_one());
    }

    const auto factory = candidate->transport().factory();
    auto transport = factory();
    REQUIRE(transport != nullptr);
    REQUIRE(*created == 2);
    REQUIRE_NOTHROW(transport->close());
}

TEST_CASE("every adapter source handles empty and failed enumeration", "[discovery][adapter][matrix]") {
    require_empty_enumeration<PCANDiscoverySource>("pcan");
    require_empty_enumeration<SLCANDiscoverySource>("slcan");
    require_empty_enumeration<MiniCANFDDiscoverySource>("minicanfd");
    require_enumeration_failure<PCANDiscoverySource>("pcan");
    require_enumeration_failure<SLCANDiscoverySource>("slcan");
    require_enumeration_failure<MiniCANFDDiscoverySource>("minicanfd");
}

TEST_CASE("every adapter source isolates an occupied channel", "[discovery][adapter][matrix]") {
    require_failed_channel_isolation<PCANDiscoverySource>("pcan");
    require_failed_channel_isolation<SLCANDiscoverySource>("slcan");
    require_failed_channel_isolation<MiniCANFDDiscoverySource>("minicanfd");
}

TEST_CASE("missing optional adapter backends are structured failures", "[discovery][adapter]") {
    PCANDiscoverySource pcan;
    SLCANDiscoverySource slcan;
    auto missing_pcan = config();
    missing_pcan.pcan.library_path = "__linkerbot_missing__/PCANBasic.dll";
    const auto pcan_report = discover_hands(missing_pcan, pcan);
    const auto slcan_report = discover_hands(config(), slcan);
    REQUIRE(pcan_report.candidates().empty());
    REQUIRE(slcan_report.candidates().empty());
    REQUIRE(pcan_report.failures().front().backend() == "pcan");
    REQUIRE(slcan_report.failures().front().backend() == "slcan");
    REQUIRE(pcan_report.failures().front().code() == "backend_unavailable");
    REQUIRE(slcan_report.failures().front().code() == "allowlist_required");
}

TEST_CASE("native SLCAN never treats common channel filters as serial permission", "[discovery][adapter][slcan]") {
    SLCANDiscoverySource source;
    auto selected = config();
    selected.channels = {"COM10"};
    const auto report = discover_hands(selected, source);
    REQUIRE(report.candidates().empty());
    REQUIRE(report.channels().empty());
    REQUIRE(report.failures().front().code() == "allowlist_required");
}

#if LINKERBOT_HAND_HAS_SLCAN
TEST_CASE("native SLCAN rejects FD and invalid serial configurations before opening", "[discovery][adapter][slcan]") {
    SLCANDiscoverySource source;
    auto selected = config();
    selected.slcan.channels = {"COM10"};
    auto report = discover_hands(selected, source);
    REQUIRE(report.channels().empty());
    REQUIRE(report.failures().front().code() == "validation");
    selected.slcan.probe_protocols = {TransportKind::kCan};
    for (const auto *port : {"COM0", "COM1x", "/tmp/ttyUSB0"}) {
        selected.slcan.channels = {port};
        report = discover_hands(selected, source);
        REQUIRE(report.channels().empty());
        REQUIRE(report.failures().front().code() == "validation");
    }
    linkerbot::can::SlcanConfig serial;
    serial.port = "COM10";
    REQUIRE_NOTHROW(make_slcan_transport_factory(serial)); // No native open on any OS.
    serial.fd = true;
    REQUIRE_THROWS(make_slcan_transport_factory(serial));
}
#endif

TEST_CASE("missing MiniCANFD dynamic library is a structured failure", "[discovery][adapter][dependency]") {
    MiniCANFDDiscoverySource source;
    auto selected = config();
    selected.minicanfd.library_path = "__linkerbot_missing__/libcanbus_missing_for_discovery_test";
    const auto report = discover_hands(selected, source);
    REQUIRE(report.channels().empty());
    REQUIRE(report.candidates().empty());
    REQUIRE(report.failures().size() == 1);
    REQUIRE(report.failures().front().backend() == "minicanfd");
    REQUIRE(report.failures().front().stage() == "library");
}

TEST_CASE("PCAN discovery applies protocol capability and native filters", "[discovery][adapter]") {
    AdapterChannelEnumerator enumerate = [](const DiscoveryConfig &) {
        return DiscoveryEnumeration{
            {DiscoveryChannel{
                 "pcan", "pcan:PCAN_USBBUS1", "PCAN_USBBUS1", TransportKind::kCan, 1'000'000U, {}, true, false},
             DiscoveryChannel{"pcan", "pcan:PCAN_USBBUS2", "PCAN_USBBUS2", TransportKind::kCanFd, 1'000'000U,
                              5'000'000U, false, true}}};
    };
    int created = 0;
    AdapterTransportFactory factory = [&created](const std::string &, TransportKind) {
        ++created;
        return std::unique_ptr<linkerbot::can::CANTransport>(std::make_unique<FakeTransport>());
    };
    PCANDiscoverySource source{enumerate, factory};

    auto selected = config();
    selected.pcan.channels = {"PCAN_USBBUS1"};
    selected.pcan.probe_protocols = {TransportKind::kCan, TransportKind::kCanFd};
    auto report = discover_hands(selected, source);
    REQUIRE(report.channels().size() == 1);
    REQUIRE(report.channels().front().native_name() == "PCAN_USBBUS1");
    REQUIRE(report.channels().front().kind() == TransportKind::kCan);
    REQUIRE(created == 1);

    created = 0;
    selected.pcan.probe_protocols = {TransportKind::kCanFd};
    report = discover_hands(selected, source);
    REQUIRE(report.channels().empty());
    REQUIRE(created == 0);
    REQUIRE(report.failures().front().code() == "unsupported_protocol");

    created = 0;
    selected.pcan.channels = {"PCAN_USBBUS2"};
    selected.pcan.probe_protocols = {TransportKind::kCan};
    report = discover_hands(selected, source);
    REQUIRE(report.channels().empty());
    REQUIRE(created == 0);
    REQUIRE(report.failures().front().code() == "unsupported_protocol");

    created = 0;
    selected.pcan.probe_protocols = {TransportKind::kCan, TransportKind::kCanFd};
    report = discover_hands(selected, source);
    REQUIRE(report.channels().size() == 1);
    REQUIRE(report.channels().front().supports_classic() == false);
    REQUIRE(report.channels().front().supports_fd() == true);
    REQUIRE(created == 1);
    REQUIRE(std::any_of(report.failures().begin(), report.failures().end(),
                        [](const DiscoveryFailure &failure) { return failure.code() == "unsupported_protocol"; }));
}

TEST_CASE("adapter enumeration consumes the same absolute discovery budget", "[discovery][adapter][timeout]") {
    int transports = 0;
    AdapterChannelEnumerator enumerate = [](const DiscoveryConfig &) {
        std::this_thread::sleep_for(std::chrono::milliseconds{15});
        return DiscoveryEnumeration{
            {DiscoveryChannel{"pcan", "pcan:slow", "slow", TransportKind::kCan, 1'000'000U, {}, true, false}}};
    };
    AdapterTransportFactory factory = [&transports](const std::string &, TransportKind) {
        ++transports;
        return std::unique_ptr<linkerbot::can::CANTransport>(std::make_unique<FakeTransport>());
    };
    PCANDiscoverySource source{std::move(enumerate), std::move(factory)};
    auto selected = config();
    selected.total_timeout = std::chrono::milliseconds{5};
    selected.pcan.probe_protocols = {TransportKind::kCan};

    const auto report = discover_hands(selected, source);
    REQUIRE(transports == 0);
    REQUIRE(report.channels().size() == 1);
    REQUIRE(report.candidates().empty());
    REQUIRE(std::any_of(report.failures().begin(), report.failures().end(),
                        [](const DiscoveryFailure &failure) { return failure.code() == "timeout"; }));
}

TEST_CASE("adapter source construction performs no enumeration or transport IO", "[discovery][adapter]") {
    int enumerations = 0;
    int transports = 0;
    AdapterChannelEnumerator enumerate = [&enumerations](const DiscoveryConfig &) {
        ++enumerations;
        return DiscoveryEnumeration{};
    };
    AdapterTransportFactory factory = [&transports](const std::string &, TransportKind) {
        ++transports;
        return std::unique_ptr<linkerbot::can::CANTransport>(std::make_unique<FakeTransport>());
    };
    REQUIRE_NOTHROW((PCANDiscoverySource{std::move(enumerate), std::move(factory)}));
    REQUIRE(enumerations == 0);
    REQUIRE(transports == 0);
    REQUIRE_NOTHROW(MiniCANFDDiscoverySource{});
}

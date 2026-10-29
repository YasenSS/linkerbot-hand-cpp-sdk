#include "hand/support/throw_as.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"
#include "hand/o30/comm_helpers.hpp"

using namespace linkerbot::hand;

namespace {
    using linkerbot::can::FakeTransport;
    using linkerbot::can::Message;

    struct TransportState {
        std::atomic<int> created{0};
        std::atomic<int> close_calls{0};
        std::mutex mutex;
        std::vector<Message> classic_sent;
        std::vector<Message> fd_sent;
        std::vector<std::shared_ptr<o30_comm::HopEndpoint>> hop_endpoints;
    };

    class TrackingFakeTransport final : public FakeTransport {
    public:
        TrackingFakeTransport(std::shared_ptr<TransportState> state, TransportKind kind)
            : state_(std::move(state)), kind_(kind) {
            ++state_->created;
        }

        ~TrackingFakeTransport() override {
            const auto messages = sent();
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto &target = kind_ == TransportKind::kCan ? state_->classic_sent : state_->fd_sent;
            target.insert(target.end(), messages.begin(), messages.end());
        }

        void close() override {
            ++state_->close_calls;
            FakeTransport::close();
        }

    private:
        std::shared_ptr<TransportState> state_;
        TransportKind kind_;
    };

    [[nodiscard]] DiscoveryConfig discovery_config() {
        DiscoveryConfig config;
        config.response_timeout = std::chrono::milliseconds{30};
        config.send_timeout = std::chrono::milliseconds{100};
        config.inter_request_delay = std::chrono::milliseconds{0};
        config.total_timeout = std::chrono::seconds{2};
        config.l30_node_ids = {1};
        config.o30_request_ids = {1};
        return config;
    }

    void install_classic_identity(FakeTransport *transport, std::string code, bool offset_fragments = false,
                                  bool l6_pcb_reply = false) {
        transport->on_request(0x28U, [code = std::move(code), offset_fragments, l6_pcb_reply](const Message &request) {
            std::vector<Message> replies;
            if (request.dlc == 0) {
                return replies;
            }
            if (request.data[0] == 0xC0) {
                const uint8_t chunk_count = offset_fragments ? 3 : 4;
                for (uint8_t chunk = 0; chunk < chunk_count; ++chunk) {
                    Message reply{};
                    reply.id = request.id;
                    reply.dlc = 8;
                    reply.data[0] = 0xC0;
                    reply.data[1] = offset_fragments ? static_cast<uint8_t>(chunk * 6) : chunk;
                    for (size_t index = 0; index < 6; ++index) {
                        const auto offset = static_cast<size_t>(chunk) * 6 + index;
                        reply.data[index + 2] = offset < code.size() ? static_cast<uint8_t>(code[offset]) : uint8_t{0};
                    }
                    replies.push_back(reply);
                }
                return replies;
            }

            Message reply{};
            reply.id = request.id;
            const bool extended_pcb = l6_pcb_reply && request.data[0] == 0xC1;
            reply.dlc = extended_pcb ? 5 : 4;
            reply.data[0] = request.data[0];
            const size_t first = extended_pcb ? 2 : 1;
            if (first == 2) {
                reply.data[1] = 1;
            }
            reply.data[first] = 1;
            reply.data[first + 1] = 2;
            reply.data[first + 2] = 3;
            replies.push_back(reply);
            return replies;
        });
    }

    void install_l30(FakeTransport *transport, bool collision = false) {
        namespace protocol = linkerbot::hand::l30::protocol;
        const std::vector<uint8_t> info{0x13, 0x00, 0x00, 0x00, 0x7B, 1, 2, 3, 2, 3, 4, 3, 4, 5, 1, 0, 'B', 'A'};
        const auto device_id =
            l30_comm::request_id(protocol::kParentConfig, protocol::kSubDeviceInfo, protocol::kAccessRead);
        auto handler = l30_comm::payload_handler(info);
        transport->on_request(device_id, [handler, collision](const Message &request) mutable {
            auto replies = handler(request);
            if (collision && !replies.empty()) {
                replies.push_back(replies.front());
            }
            return replies;
        });

        const std::string code = "LHT30-06-123-L-B-3-A";
        transport->on_request(
            l30_comm::request_id(protocol::kParentConfig, protocol::kSubProductCode, protocol::kAccessRead),
            l30_comm::payload_handler(std::vector<uint8_t>(code.begin(), code.end())));
    }

    [[nodiscard]] SocketCANDiscoverySource::TransportFactory
    full_factory(const std::shared_ptr<TransportState> &state) {
        return [state](const std::string &, TransportKind kind) {
            auto transport = std::make_unique<TrackingFakeTransport>(state, kind);
            if (kind == TransportKind::kCan) {
                install_classic_identity(transport.get(), "LHL6-1-123-L", false, true);
            } else {
                install_l30(transport.get());
                auto hop = std::make_shared<o30_comm::HopEndpoint>();
                hop->seed_defaults();
                hop->put_ascii(o30_comm::protocol::kMiProductInfo, 0xD9, 8, "left");
                o30_comm::attach_hop(transport.get(), hop);
                std::lock_guard<std::mutex> lock(state->mutex);
                state->hop_endpoints.push_back(std::move(hop));
            }
            return std::unique_ptr<linkerbot::can::CANTransport>(std::move(transport));
        };
    }

    [[nodiscard]] size_t model_count(const DiscoveryReport &report, Model model) {
        return static_cast<size_t>(
            std::count_if(report.candidates().begin(), report.candidates().end(),
                          [model](const DiscoveryCandidate &candidate) { return candidate.model() == model; }));
    }
} // namespace

TEST_CASE("SocketCAN discovery finds classic L30 and O30 identities", "[discovery][socketcan][comm]") {
    auto state = std::make_shared<TransportState>();
    SocketCANDiscoverySource source{
        [] {
            return std::vector<DiscoveryChannel>{
                DiscoveryChannel{"fake-socketcan", "vcan0", TransportKind::kCanFd, 1'000'000U, 5'000'000U, true, true}};
        },
        full_factory(state)};

    const auto report = discover_hands(discovery_config(), source);
    REQUIRE(report.failures().empty());
    REQUIRE(report.candidates().size() == 3);
    REQUIRE(report.verified_candidates().size() == 3);
    REQUIRE(model_count(report, Model::kL6) == 1);
    REQUIRE(model_count(report, Model::kL30) == 1);
    REQUIRE(model_count(report, Model::kO30) == 1);
    REQUIRE(state->created == 2);
    REQUIRE(state->close_calls >= 2);

    for (const auto &candidate : report.candidates()) {
        REQUIRE(candidate.channel().name() == "vcan0");
        const auto config = candidate.make_config();
        REQUIRE(config.model == candidate.model());
        REQUIRE(config.side == candidate.side());
        REQUIRE(config.interface_name == "vcan0");
        if (candidate.model() == Model::kL30) {
            REQUIRE(candidate.address().node_id() == std::optional<uint8_t>{1});
            REQUIRE(config.address == 1);
        } else {
            REQUIRE(config.address == candidate.address().request_id());
        }
        auto hand = candidate.create_hand_device();
        REQUIRE(hand != nullptr);
        REQUIRE_FALSE(hand->is_open());
    }
    REQUIRE(state->created == 2);
}

TEST_CASE("SocketCAN discovery handles empty and failed enumeration", "[discovery][socketcan][comm][matrix]") {
    std::atomic<int> created{0};
    const auto factory = [&created](const std::string &, TransportKind) {
        ++created;
        return std::unique_ptr<linkerbot::can::CANTransport>(std::make_unique<FakeTransport>());
    };

    SocketCANDiscoverySource empty{[] { return std::vector<DiscoveryChannel>{}; }, factory};
    const auto empty_report = discover_hands(discovery_config(), empty);
    REQUIRE(created == 0);
    REQUIRE(empty_report.channels().empty());
    REQUIRE(empty_report.candidates().empty());
    REQUIRE(empty_report.failures().empty());

    SocketCANDiscoverySource failed{
        []() { return test_util::throw_as<std::vector<DiscoveryChannel>>(std::runtime_error("enumeration failed")); },
        factory};
    const auto failed_report = discover_hands(discovery_config(), failed);
    REQUIRE(created == 0);
    REQUIRE(failed_report.channels().empty());
    REQUIRE(failed_report.candidates().empty());
    REQUIRE(failed_report.failures().size() == 1);
    REQUIRE(failed_report.failures().front().operation() == "enumerate");
    REQUIRE(failed_report.failures().front().backend() == "socketcan");
}

TEST_CASE("SocketCAN discovery preserves an O30 missing-side identity for explicit completion",
          "[discovery][socketcan][comm][partial]") {
    std::atomic<int> created{0};
    auto hop = std::make_shared<o30_comm::HopEndpoint>();
    hop->seed_defaults();
    SocketCANDiscoverySource source{[] {
                                        return std::vector<DiscoveryChannel>{
                                            DiscoveryChannel{"fake-socketcan", "canfd0", TransportKind::kCanFd,
                                                             1'000'000U, 5'000'000U, false, true}};
                                    },
                                    [&created, hop](const std::string &, TransportKind kind) {
                                        ++created;
                                        auto transport = std::make_unique<FakeTransport>();
                                        if (kind == TransportKind::kCanFd) {
                                            o30_comm::attach_hop(transport.get(), hop);
                                        }
                                        return std::unique_ptr<linkerbot::can::CANTransport>(std::move(transport));
                                    }};

    const auto report = discover_hands(discovery_config(), source);
    REQUIRE(report.candidates().size() == 1);
    const auto &candidate = report.require_one(true);
    REQUIRE(candidate.status() == DiscoveryStatus::kPartial);
    REQUIRE(candidate.model() == Model::kO30);
    REQUIRE(candidate.side() == HandSide::kUnknown);
    REQUIRE(candidate.problems().size() == 1);
    REQUIRE(candidate.problems().front().code() == "missing_side");
    REQUIRE(candidate.identity().device_uid == std::optional<std::string>{"DEVICE-UID-O30"});

    const auto after_discovery = created.load();
    auto hand = candidate.create_hand_device(HandRuntimeConfig{}, HandSide::kLeft);
    REQUIRE(created.load() == after_discovery);

    hop->put_ascii(o30_comm::protocol::kMiProductInfo, 0x39, 0x20, "OTHER-UID-O30");
    REQUIRE_THROWS_AS(hand->open(), IdentityMismatchError);
    REQUIRE_FALSE(hand->is_open());

    hop->put_ascii(o30_comm::protocol::kMiProductInfo, 0x39, 0x20, "DEVICE-UID-O30");
    REQUIRE_NOTHROW(hand->open());
    REQUIRE(hand->get_identity()->side == HandSide::kLeft);
    REQUIRE(hand->get_capabilities().side == HandSide::kLeft);
    hand->close();

    // Side evidence returned during open cannot be overridden by the explicit
    // request used to complete the partial candidate.
    hop->put_ascii(o30_comm::protocol::kMiProductInfo, 0xD9, 8, "left");
    auto wrong = candidate.create_hand_device(HandRuntimeConfig{}, HandSide::kRight);
    REQUIRE_THROWS_AS(wrong->open(), IdentityMismatchError);
    REQUIRE_FALSE(wrong->is_open());
}

TEST_CASE("SocketCAN discovery verifies every classic hand model", "[discovery][socketcan][comm]") {
    struct Profile {
        Model model;
        const char *product_code;
        bool offset_fragments;
        bool l6_pcb_reply;
    };
    const std::vector<Profile> profiles{{Model::kL6, "LHL6-1-123-L", false, true},
                                        {Model::kO6, "LHO6-1-123-L", false, false},
                                        {Model::kL20Lite, "LHT10-1-123-L", true, false},
                                        {Model::kL20, "LHT20-1-123-L", false, false}};

    for (const auto &profile : profiles) {
        INFO("model=" << static_cast<int>(profile.model));
        SocketCANDiscoverySource source{[] {
                                            return std::vector<DiscoveryChannel>{DiscoveryChannel{
                                                "fake-socketcan", "can0", TransportKind::kCan, {}, {}, true, false}};
                                        },
                                        [profile](const std::string &, TransportKind) {
                                            auto transport = std::make_unique<FakeTransport>();
                                            install_classic_identity(transport.get(), profile.product_code,
                                                                     profile.offset_fragments, profile.l6_pcb_reply);
                                            return std::unique_ptr<linkerbot::can::CANTransport>(std::move(transport));
                                        }};

        const auto report = discover_hands(discovery_config(), source);
        REQUIRE(report.failures().empty());
        REQUIRE(report.candidates().size() == 1);
        REQUIRE(report.candidates().front().status() == DiscoveryStatus::kVerified);
        REQUIRE(report.candidates().front().model() == profile.model);
    }
}

TEST_CASE("SocketCAN discovery reports an L30 address collision", "[discovery][socketcan][comm]") {
    SocketCANDiscoverySource source{[] {
                                        return std::vector<DiscoveryChannel>{DiscoveryChannel{
                                            "fake-socketcan", "canfd0", TransportKind::kCanFd, {}, {}, false, true}};
                                    },
                                    [](const std::string &, TransportKind) {
                                        auto transport = std::make_unique<FakeTransport>();
                                        install_l30(transport.get(), true);
                                        return std::unique_ptr<linkerbot::can::CANTransport>(std::move(transport));
                                    }};

    const auto report = discover_hands(discovery_config(), source);
    REQUIRE(report.candidates().size() == 1);
    const auto &candidate = report.candidates().front();
    REQUIRE(candidate.model() == Model::kL30);
    REQUIRE(candidate.status() == DiscoveryStatus::kPartial);
    REQUIRE(candidate.problems().size() == 1);
    REQUIRE(candidate.problems().front().code() == "address_collision");
}

TEST_CASE("SocketCAN discovery isolates a broken channel", "[discovery][socketcan][comm]") {
    SocketCANDiscoverySource source{
        [] {
            return std::vector<DiscoveryChannel>{
                DiscoveryChannel{"fake-socketcan", "broken0", TransportKind::kCan, {}, {}, true, false},
                DiscoveryChannel{"fake-socketcan", "can0", TransportKind::kCan, {}, {}, true, false}};
        },
        [](const std::string &name, TransportKind) {
            if (name == "broken0") {
                throw std::runtime_error("adapter is busy");
            }
            auto transport = std::make_unique<FakeTransport>();
            install_classic_identity(transport.get(), "LHL6-1-123-L", false, true);
            return std::unique_ptr<linkerbot::can::CANTransport>(std::move(transport));
        }};

    const auto report = discover_hands(discovery_config(), source);
    REQUIRE(report.channels().size() == 2);
    REQUIRE(report.failures().size() == 1);
    REQUIRE(report.failures().front().channel() == std::optional<std::string>{"broken0"});
    REQUIRE(report.candidates().size() == 1);
    REQUIRE(report.candidates().front().model() == Model::kL6);
}

TEST_CASE("SocketCAN discovery honors channel filters and timeout silence", "[discovery][socketcan][comm]") {
    std::atomic<int> created{0};
    SocketCANDiscoverySource source{
        [] {
            return std::vector<DiscoveryChannel>{
                DiscoveryChannel{"fake-socketcan", "can0", TransportKind::kCan, {}, {}, true, false}};
        },
        [&created](const std::string &, TransportKind) {
            ++created;
            return std::unique_ptr<linkerbot::can::CANTransport>(std::make_unique<FakeTransport>());
        }};

    auto config = discovery_config();
    config.channels = {"missing0"};
    auto report = discover_hands(config, source);
    REQUIRE(report.candidates().empty());
    REQUIRE(report.failures().size() == 1);
    REQUIRE(report.failures().front().code() == "channel_not_found");
    REQUIRE(created == 0);

    config.channels = {"can0"};
    report = discover_hands(config, source);
    REQUIRE(report.candidates().empty());
    REQUIRE(report.failures().size() == 1);
    REQUIRE(report.failures().front().code() == "no_response");
    REQUIRE(report.failures().front().channel() == std::optional<std::string>{"can0"});
    REQUIRE(created == 1);
}

TEST_CASE("SocketCAN discovery reports channels with no compatible protocol", "[discovery][socketcan][comm]") {
    std::atomic<int> created{0};
    SocketCANDiscoverySource source{
        [] {
            return std::vector<DiscoveryChannel>{
                DiscoveryChannel{"fake-socketcan", "disabled0", TransportKind::kCan, {}, {}, false, false}};
        },
        [&created](const std::string &, TransportKind) {
            ++created;
            return std::unique_ptr<linkerbot::can::CANTransport>(std::make_unique<FakeTransport>());
        }};

    const auto report = discover_hands(discovery_config(), source);
    REQUIRE(created == 0);
    REQUIRE(report.candidates().empty());
    REQUIRE(report.failures().size() == 1);
    REQUIRE(report.failures().front().code() == "unsupported_protocol");
    REQUIRE(report.failures().front().stage() == "select_protocol");
}

TEST_CASE("SocketCAN discovery opens the backend-native channel name", "[discovery][socketcan][comm]") {
    std::string opened_channel;
    SocketCANDiscoverySource source{
        [] {
            return std::vector<DiscoveryChannel>{
                DiscoveryChannel{"socketcan", "socketcan:can0", "can0", TransportKind::kCan, {}, {}, true, false}};
        },
        [&opened_channel](const std::string &channel, TransportKind) {
            opened_channel = channel;
            return std::unique_ptr<linkerbot::can::CANTransport>(std::make_unique<FakeTransport>());
        }};

    auto config = discovery_config();
    config.channels = {"can0"};
    const auto report = discover_hands(config, source);
    REQUIRE(report.channels().size() == 1);
    REQUIRE(report.channels().front().name() == "socketcan:can0");
    REQUIRE(report.channels().front().native_name() == "can0");
    REQUIRE(opened_channel == "can0");
}

TEST_CASE("SocketCAN discovery keeps an invalid L30 identity as partial", "[discovery][socketcan][comm]") {
    namespace protocol = linkerbot::hand::l30::protocol;
    SocketCANDiscoverySource source{[] {
                                        return std::vector<DiscoveryChannel>{DiscoveryChannel{
                                            "fake-socketcan", "canfd0", TransportKind::kCanFd, {}, {}, false, true}};
                                    },
                                    [](const std::string &, TransportKind) {
                                        auto transport = std::make_unique<FakeTransport>();
                                        const auto request_id = l30_comm::request_id(
                                            protocol::kParentConfig, protocol::kSubDeviceInfo, protocol::kAccessRead);
                                        transport->on_request(request_id, [](const Message &request) {
                                            Message malformed{};
                                            malformed.id = protocol::expected_response_id(request.id);
                                            malformed.is_extended = true;
                                            malformed.is_fd = true;
                                            malformed.dlc = 1;
                                            malformed.data[0] = 0xFF;
                                            return std::vector<Message>{malformed};
                                        });
                                        return std::unique_ptr<linkerbot::can::CANTransport>(std::move(transport));
                                    }};

    const auto report = discover_hands(discovery_config(), source);
    REQUIRE(report.failures().empty());
    REQUIRE(report.candidates().size() == 1);
    const auto &candidate = report.candidates().front();
    REQUIRE(candidate.model() == Model::kL30);
    REQUIRE(candidate.status() == DiscoveryStatus::kPartial);
    REQUIRE(candidate.problems().size() == 1);
    REQUIRE(candidate.problems().front().code() == "protocol_error");
}

TEST_CASE("SocketCAN discovery reports temporary transport close failure", "[discovery][socketcan][comm]") {
    SocketCANDiscoverySource source{[] {
                                        return std::vector<DiscoveryChannel>{DiscoveryChannel{
                                            "fake-socketcan", "can0", TransportKind::kCan, {}, {}, true, false}};
                                    },
                                    [](const std::string &, TransportKind) {
                                        auto transport = std::make_unique<FakeTransport>();
                                        transport->fail_next_closes(1);
                                        return std::unique_ptr<linkerbot::can::CANTransport>(std::move(transport));
                                    }};

    const auto report = discover_hands(discovery_config(), source);
    REQUIRE(report.candidates().empty());
    REQUIRE(report.failures().size() == 1);
    REQUIRE(report.failures().front().operation() == "close_probe_session");
    REQUIRE(report.failures().front().channel() == std::optional<std::string>{"can0"});
}

TEST_CASE("SocketCAN discovery can exercise an opt-in Linux vcan channel", "[discovery][socketcan][comm][vcan]") {
#if defined(__linux__)
    const auto *channel = std::getenv("LINKERBOT_TEST_VCAN_INTERFACE");
    if (channel == nullptr || std::string{channel}.empty()) {
        SUCCEED("set LINKERBOT_TEST_VCAN_INTERFACE to run the vcan integration path");
        return;
    }

    auto config = discovery_config();
    config.channels = {channel};
    const auto report = discover_hands(config);
    REQUIRE(report.channels().size() == 1);
    REQUIRE(report.channels().front().provider() == "socketcan");
    REQUIRE(report.channels().front().name() == channel);
#else
    SUCCEED("SocketCAN vcan integration is Linux-only");
#endif
}

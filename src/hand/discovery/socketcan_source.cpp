#include <linkerbot/hand/discovery.hpp>

#include "../identity/probe.hpp"

#include <algorithm>
#include <any>
#include <cerrno>
#include <chrono>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <typeinfo>
#include <utility>
#include <vector>

#include <linkerbot/can/errors.hpp>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/hand/l30/protocol_backend.hpp>
#include <linkerbot/hand/l30/version.hpp>
#include <linkerbot/hand/o30/protocol.hpp>
#include <linkerbot/hand/o30/transaction.hpp>
#include <linkerbot/transport/session.hpp>

#if defined(__linux__)
#include <fstream>
#include <ifaddrs.h>
#include <net/if.h>

#include <linux/can.h>

#include <linkerbot/can/socketcan_transport.hpp>
#endif

namespace linkerbot::hand {
    namespace {
        using Clock = std::chrono::steady_clock;
        using RouteMessages = std::map<transport::RouteKey, std::vector<can::Message>>;

        class InboxGuard {
        public:
            explicit InboxGuard(std::shared_ptr<transport::RouteInbox> inbox) : inbox_(std::move(inbox)) {}

            ~InboxGuard() {
                if (inbox_) {
                    inbox_->close();
                }
            }

            [[nodiscard]] const std::shared_ptr<transport::RouteInbox> &get() const { return inbox_; }

        private:
            std::shared_ptr<transport::RouteInbox> inbox_;
        };

        [[nodiscard]] std::chrono::milliseconds remaining(Clock::time_point deadline,
                                                          std::chrono::milliseconds maximum) {
            const auto now = Clock::now();
            if (now >= deadline) {
                throw TimeoutError("discovery total time budget exhausted");
            }
            const auto budget = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            return std::max(std::chrono::milliseconds{1}, std::min(maximum, budget));
        }

        [[nodiscard]] std::string error_code(const std::exception &error) {
            if (const auto *hand = dynamic_cast<const HandError *>(&error)) {
                return std::string(error_code_name(hand->code()));
            }
            if (const auto *can_error = dynamic_cast<const can::CANError *>(&error)) {
                return can::error_code_name(can_error->code());
            }
            return "internal";
        }

        void record_failure(std::vector<DiscoveryFailure> &failures, const std::string &backend, std::string operation,
                            const std::exception &error, std::optional<std::string> channel = {},
                            std::optional<std::string> native_channel = {}) {
            const auto stage = operation;
            std::optional<int> native_status;
            if (const auto *can_error = dynamic_cast<const can::CANError *>(&error)) {
                native_status = can_error->native_status();
            }
            failures.emplace_back(std::move(operation), error_code(error), error.what(), std::move(channel), backend,
                                  std::move(stage), std::move(native_channel), typeid(error).name(), native_status);
        }

        [[nodiscard]] DiscoveryProblem problem_from_error(const std::exception &error) {
            if (dynamic_cast<const IdentityMismatchError *>(&error)) {
                return {"identity_mismatch", error.what()};
            }
            if (dynamic_cast<const TimeoutError *>(&error) || dynamic_cast<const can::TimeoutError *>(&error)) {
                return {"identity_timeout", error.what()};
            }
            if (dynamic_cast<const ProtocolError *>(&error)) {
                return {"protocol_error", error.what()};
            }
            return {"identity_error", error.what()};
        }

        [[nodiscard]] DiscoveryTransportSpec transport_spec(const DiscoveryChannel &channel,
                                                            TransportKind wire_protocol,
                                                            const SocketCANDiscoverySource::TransportFactory &factory) {
            const auto name = channel.name();
            const auto native_name = channel.native_name();
            return DiscoveryTransportSpec{
                channel.provider(),
                name,
                wire_protocol,
                [factory, native_name, wire_protocol] { return factory(native_name, wire_protocol); },
                channel.nominal_bitrate(),
                wire_protocol == TransportKind::kCanFd ? channel.data_bitrate() : std::optional<uint32_t>{},
                native_name,
                channel.configuration_id()};
        }

        [[nodiscard]] RouteMessages collect(const std::shared_ptr<transport::Session> &session,
                                            const std::vector<transport::RouteKey> &routes,
                                            std::vector<can::Message> frames, const DiscoveryConfig &config,
                                            Clock::time_point total_deadline) {
            InboxGuard guard{session->subscribe_routes(routes, config.route_queue_capacity)};

            transport::MessageBatch batch;
            batch.frames = std::move(frames);
            batch.gap = std::chrono::duration_cast<std::chrono::microseconds>(config.inter_request_delay);
            transport::SendOptions options;
            options.deadline = Clock::now() + remaining(total_deadline, config.send_timeout);
            options.endpoint = "discovery";
            options.family = "identity";
            const auto receipt = session->submit(std::move(batch), options);
            const auto send_result = receipt.wait_until(options.deadline);
            if (send_result.outcome != transport::SendOutcome::dispatched) {
                if (send_result.error) {
                    std::rethrow_exception(send_result.error);
                }
                throw DiscoveryError("identity probe was not dispatched");
            }

            const auto response_deadline = Clock::now() + remaining(total_deadline, config.response_timeout);
            RouteMessages grouped;
            while (Clock::now() < response_deadline) {
                const auto routed = guard.get()->receive_until(response_deadline);
                if (!routed) {
                    break;
                }
                grouped[{routed->message.id, routed->message.is_extended}].push_back(routed->message);
            }
            if (guard.get()->health().overflow != 0) {
                throw DiscoveryError("discovery receive queue overflowed");
            }
            return grouped;
        }

        [[nodiscard]] DeviceIdentity partial_identity(Model model, HandSide side, std::optional<uint32_t> address = {},
                                                      std::optional<std::string> product_code = {}) {
            DeviceIdentity identity;
            identity.model = model;
            identity.side = side;
            identity.address = address;
            identity.product_code = std::move(product_code);
            identity.timestamp = Clock::now();
            return identity;
        }

        void add_partial(std::vector<DiscoveryCandidate> &candidates, const DiscoveryChannel &channel,
                         TransportKind wire_protocol, DiscoveryAddress address, DeviceIdentity identity,
                         const SocketCANDiscoverySource::TransportFactory &factory, DiscoveryProblem problem) {
            candidates.emplace_back(channel, wire_protocol, std::move(address), DiscoveryStatus::kPartial,
                                    std::move(identity), transport_spec(channel, wire_protocol, factory), std::nullopt,
                                    std::vector<DiscoveryProblem>{std::move(problem)});
        }

        void add_verified(std::vector<DiscoveryCandidate> &candidates, const DiscoveryChannel &channel,
                          TransportKind wire_protocol, DiscoveryAddress address, DeviceIdentity identity,
                          const SocketCANDiscoverySource::TransportFactory &factory) {
            candidates.emplace_back(channel, wire_protocol, std::move(address), DiscoveryStatus::kVerified, identity,
                                    transport_spec(channel, wire_protocol, factory),
                                    declared_capabilities(identity.model, identity.side));
        }

        [[nodiscard]] HandSide classic_side(uint32_t address) {
            if (address == 0x28U) {
                return HandSide::kLeft;
            }
            if (address == 0x27U) {
                return HandSide::kRight;
            }
            throw ValidationError("unknown classic discovery address");
        }

        [[nodiscard]] bool has_duplicate_classic_fragment(const std::vector<can::Message> &messages, uint32_t address) {
            std::set<uint8_t> markers;
            for (const auto &message : messages) {
                if (message.id != address || message.is_extended || message.is_fd || message.dlc != 8 ||
                    message.data[0] != 0xC0) {
                    continue;
                }
                if (!markers.insert(message.data[1]).second) {
                    return true;
                }
            }
            return false;
        }

        void probe_classic(const std::shared_ptr<transport::Session> &session, const DiscoveryChannel &channel,
                           const DiscoveryConfig &config, Clock::time_point total_deadline,
                           const SocketCANDiscoverySource::TransportFactory &factory,
                           std::vector<DiscoveryCandidate> &candidates) {
            constexpr uint32_t addresses[]{0x28U, 0x27U};
            std::vector<transport::RouteKey> routes;
            std::vector<can::Message> requests;
            for (const auto address : addresses) {
                routes.push_back({address, false});
                const auto plan = identity_probe::classic_product_plan(address);
                requests.push_back(plan.frames.front());
            }
            const auto grouped = collect(session, routes, std::move(requests), config, total_deadline);

            for (const auto address : addresses) {
                const auto found = grouped.find({address, false});
                if (found == grouped.end() || found->second.empty()) {
                    continue;
                }

                const auto side = classic_side(address);
                const DiscoveryAddress discovery_address{"classic_can_id", address, address, false};
                if (has_duplicate_classic_fragment(found->second, address)) {
                    add_partial(candidates, channel, TransportKind::kCan, discovery_address,
                                partial_identity(Model::kUnknown, side, address), factory,
                                {"address_collision", "multiple classic identity responses used the same CAN ID"});
                    continue;
                }

                std::optional<std::string> discovered_product_code;
                auto discovered_model = Model::kUnknown;
                try {
                    auto plan = identity_probe::classic_product_plan(address);
                    bool matched = false;
                    for (const auto &message : found->second) {
                        auto progress = plan.consume(message);
                        if (progress.kind != protocol::ProgressKind::unmatched) {
                            matched = true;
                        }
                        if (progress.kind == protocol::ProgressKind::failed) {
                            if (progress.error) {
                                std::rethrow_exception(progress.error);
                            }
                            throw ProtocolError("classic identity parser failed");
                        }
                        if (progress.kind == protocol::ProgressKind::complete) {
                            discovered_product_code = std::any_cast<std::string>(progress.value);
                        }
                    }
                    if (!matched) {
                        continue;
                    }
                    if (!discovered_product_code) {
                        add_partial(candidates, channel, TransportKind::kCan, discovery_address,
                                    partial_identity(Model::kUnknown, side, address), factory,
                                    {"product_code_incomplete", "classic product code has missing fragments"});
                        continue;
                    }

                    discovered_model = identity_probe::classic_model_from_product_code(*discovered_product_code);
                    if (discovered_model == Model::kUnknown) {
                        add_partial(candidates, channel, TransportKind::kCan, discovery_address,
                                    partial_identity(discovered_model, side, address, *discovered_product_code),
                                    factory, {"unknown_model", "classic product code has an unknown model prefix"});
                        continue;
                    }

                    const auto runtime =
                        runtime::DeviceRuntime::acquire(session, "discovery/classic/" + std::to_string(address),
                                                        {{address, false}}, config.route_queue_capacity);
                    auto identity = identity_probe::probe(runtime, {discovered_model, side, address, 0},
                                                          remaining(total_deadline, config.response_timeout * 4));
                    if (!identity.product_code || *identity.product_code != *discovered_product_code) {
                        throw IdentityMismatchError("classic product code changed during identity verification");
                    }
                    add_verified(candidates, channel, TransportKind::kCan, discovery_address, std::move(identity),
                                 factory);
                } catch (const std::exception &error) {
                    add_partial(candidates, channel, TransportKind::kCan, discovery_address,
                                partial_identity(discovered_model, side, address, discovered_product_code), factory,
                                problem_from_error(error));
                }
            }
        }

        [[nodiscard]] can::Message l30_request(uint8_t node_id, uint8_t subcommand) {
            return l30::protocol::request_frame(node_id, 0, l30::protocol::kParentConfig, subcommand,
                                                l30::protocol::kAccessRead, l30::protocol::empty_request_payload(),
                                                l30::protocol::kEmptyRequestBytes);
        }

        void probe_l30(const std::shared_ptr<transport::Session> &session, const DiscoveryChannel &channel,
                       const DiscoveryConfig &config, Clock::time_point total_deadline,
                       const SocketCANDiscoverySource::TransportFactory &factory,
                       std::vector<DiscoveryCandidate> &candidates) {
            std::vector<transport::RouteKey> routes;
            std::vector<can::Message> requests;
            std::map<transport::RouteKey, uint8_t> nodes;
            for (const auto node_id : config.l30_node_ids) {
                auto request = l30_request(node_id, l30::protocol::kSubDeviceInfo);
                const transport::RouteKey route{l30::protocol::expected_response_id(request.id), true};
                routes.push_back(route);
                requests.push_back(request);
                nodes.emplace(route, node_id);
            }
            const auto grouped = collect(session, routes, std::move(requests), config, total_deadline);

            for (const auto &[route, node_id] : nodes) {
                const auto found = grouped.find(route);
                if (found == grouped.end() || found->second.empty()) {
                    continue;
                }
                const auto device_request = l30_request(node_id, l30::protocol::kSubDeviceInfo);
                const auto product_request = l30_request(node_id, l30::protocol::kSubProductCode);
                const DiscoveryAddress address{"l30_node_id", device_request.id, route.id, true, node_id, uint8_t{0}};
                if (found->second.size() != 1) {
                    add_partial(candidates, channel, TransportKind::kCanFd, address,
                                partial_identity(Model::kL30, HandSide::kUnknown, node_id), factory,
                                {"address_collision", "multiple L30 DeviceInfo responses used the same node ID"});
                    continue;
                }

                auto discovered_side = HandSide::kUnknown;
                try {
                    const auto &message = found->second.front();
                    if (!l30::protocol::valid_frame(message, route.id)) {
                        throw ProtocolError("invalid L30 DeviceInfo discovery response");
                    }
                    const std::vector<uint8_t> response(message.data.begin(), message.data.begin() + message.dlc);
                    const auto initial = l30::decode_device_info_response(response);
                    discovered_side = initial.hand_side == l30::HandSide::kLeft ? HandSide::kLeft : HandSide::kRight;

                    const auto product_route =
                        transport::RouteKey{l30::protocol::expected_response_id(product_request.id), true};
                    const auto runtime =
                        runtime::DeviceRuntime::acquire(session, "discovery/l30/" + std::to_string(node_id),
                                                        {route, product_route}, config.route_queue_capacity);
                    auto identity = identity_probe::probe(runtime, {Model::kL30, discovered_side, node_id, 0},
                                                          remaining(total_deadline, config.response_timeout * 2));
                    add_verified(candidates, channel, TransportKind::kCanFd, address, std::move(identity), factory);
                } catch (const std::exception &error) {
                    add_partial(candidates, channel, TransportKind::kCanFd, address,
                                partial_identity(Model::kL30, discovered_side, node_id), factory,
                                problem_from_error(error));
                }
            }
        }

        void probe_o30(const std::shared_ptr<transport::Session> &session, const DiscoveryChannel &channel,
                       const DiscoveryConfig &config, Clock::time_point total_deadline,
                       const SocketCANDiscoverySource::TransportFactory &factory,
                       std::vector<DiscoveryCandidate> &candidates) {
            std::vector<transport::RouteKey> routes;
            std::vector<can::Message> requests;
            std::map<transport::RouteKey, uint32_t> endpoints;
            for (const auto request_id : config.o30_request_ids) {
                const transport::RouteKey route{o30::protocol::expected_response_id(request_id), false};
                routes.push_back(route);
                requests.push_back(
                    o30::protocol::build_message(request_id, o30::protocol::kMiProductInfo, 0, 0x10, {}, false));
                endpoints.emplace(route, request_id);
            }
            const auto grouped = collect(session, routes, std::move(requests), config, total_deadline);

            for (const auto &[route, request_id] : endpoints) {
                const auto found = grouped.find(route);
                if (found == grouped.end() || found->second.empty()) {
                    continue;
                }
                const DiscoveryAddress address{"o30_hop_endpoint", request_id, route.id, false};
                if (found->second.size() != 1) {
                    add_partial(candidates, channel, TransportKind::kCanFd, address,
                                partial_identity(Model::kO30, HandSide::kUnknown), factory,
                                {"address_collision", "multiple O30 product responses used the same HOP endpoint"});
                    continue;
                }

                try {
                    const auto &message = found->second.front();
                    if (message.id != route.id || message.is_extended || !message.is_fd) {
                        throw ProtocolError("invalid O30 product discovery response");
                    }
                    const auto frame = o30::protocol::parse_frame(message.data.data(), message.dlc);
                    if (frame.mi != o30::protocol::kMiProductInfo || frame.rts || frame.si != 0 || frame.edl != 0x10) {
                        throw ProtocolError("unexpected O30 product discovery response");
                    }
                    const auto model_text = o30::protocol::decode_ascii(frame.payload);
                    const auto model_parts = identity_detail::split_product(model_text);
                    if (model_parts.empty()) {
                        throw IdentityProtocolError("O30 product model is empty");
                    }
                    const auto model = identity_detail::upper(model_parts.front());
                    if (model != "O30" && model != "O30I") {
                        throw IdentityMismatchError("unexpected O30 product model: " + model_text);
                    }

                    const auto runtime = runtime::DeviceRuntime::acquire(
                        session, "discovery/o30/" + std::to_string(request_id), {route}, config.route_queue_capacity);
                    auto identity = identity_probe::probe(runtime, {Model::kO30, HandSide::kUnknown, request_id, 0},
                                                          remaining(total_deadline, config.response_timeout * 4));
                    if (identity.side == HandSide::kUnknown) {
                        add_partial(candidates, channel, TransportKind::kCanFd, address, std::move(identity), factory,
                                    {"missing_side", "O30 identity did not report a hand side"});
                        continue;
                    }
                    add_verified(candidates, channel, TransportKind::kCanFd, address, std::move(identity), factory);
                } catch (const std::exception &error) {
                    add_partial(candidates, channel, TransportKind::kCanFd, address,
                                partial_identity(Model::kO30, HandSide::kUnknown), factory, problem_from_error(error));
                }
            }
        }

        [[nodiscard]] std::vector<DiscoveryChannel> default_channels() {
#if defined(__linux__)
            ifaddrs *raw = nullptr;
            if (::getifaddrs(&raw) != 0) {
                throw can::TransportIOError("failed to enumerate network interfaces",
                                            std::error_code(errno, std::generic_category()));
            }
            std::unique_ptr<ifaddrs, decltype(&::freeifaddrs)> addresses{raw, &::freeifaddrs};
            std::set<std::string> names;
            for (auto *item = addresses.get(); item != nullptr; item = item->ifa_next) {
                if (!item->ifa_addr || item->ifa_addr->sa_family != AF_CAN || (item->ifa_flags & IFF_UP) == 0 ||
                    !item->ifa_name) {
                    continue;
                }
                names.emplace(item->ifa_name);
            }

            std::vector<DiscoveryChannel> channels;
            for (const auto &name : names) {
                std::ifstream mtu_file{"/sys/class/net/" + name + "/mtu"};
                unsigned mtu = CAN_MTU;
                if (!(mtu_file >> mtu)) {
                    mtu = CAN_MTU;
                }
                const bool supports_fd = mtu >= CANFD_MTU;
                channels.emplace_back("socketcan", name, name,
                                      supports_fd ? TransportKind::kCanFd : TransportKind::kCan, std::nullopt,
                                      std::nullopt, true, supports_fd);
            }
            return channels;
#else
            throw UnsupportedOperationError("SocketCAN discovery is only available on Linux");
#endif
        }

        [[nodiscard]] std::unique_ptr<can::CANTransport> default_transport(const std::string &channel,
                                                                           TransportKind wire_protocol) {
#if defined(__linux__)
            return std::make_unique<can::SocketCANTransport>(channel, wire_protocol == TransportKind::kCanFd);
#else
            (void)channel;
            (void)wire_protocol;
            throw UnsupportedOperationError("SocketCAN discovery is only available on Linux");
#endif
        }

        [[nodiscard]] std::vector<DiscoveryChannel> selected_channels(const std::vector<DiscoveryChannel> &available,
                                                                      const DiscoveryConfig &config,
                                                                      const std::string &backend,
                                                                      std::vector<DiscoveryFailure> &failures) {
            if (config.channels.empty()) {
                return available;
            }

            std::vector<DiscoveryChannel> selected;
            for (const auto &requested : config.channels) {
                const auto found =
                    std::find_if(available.begin(), available.end(), [&requested](const DiscoveryChannel &channel) {
                        return channel.name() == requested || channel.native_name() == requested;
                    });
                if (found != available.end()) {
                    selected.push_back(*found);
                } else {
                    failures.emplace_back("enumerate", "channel_not_found",
                                          "requested discovery channel was not found or is not available", requested,
                                          backend, "enumerate", requested, "DiscoveryError");
                }
            }
            return selected;
        }

        template <typename Probe>
        void scan_wire(const DiscoveryChannel &channel, TransportKind wire_protocol, const DiscoveryConfig &config,
                       Clock::time_point total_deadline, const std::string &backend,
                       const SocketCANDiscoverySource::TransportFactory &factory,
                       std::vector<DiscoveryCandidate> &candidates, std::vector<DiscoveryFailure> &failures,
                       Probe probe) {
            std::shared_ptr<transport::Session> session;
            try {
                const auto native_name = channel.native_name();
                session = transport::Session::create(
                    [factory, native_name, wire_protocol] { return factory(native_name, wire_protocol); });
                if (Clock::now() >= total_deadline) {
                    throw TimeoutError("discovery total budget exhausted while opening transport");
                }
                probe(session, channel, config, total_deadline, factory, candidates);
            } catch (const std::exception &error) {
                record_failure(failures, backend, "probe_identity", error, channel.name(), channel.native_name());
            }

            if (session) {
                try {
                    session->close();
                } catch (const std::exception &error) {
                    record_failure(failures, backend, "close_probe_session", error, channel.name(),
                                   channel.native_name());
                }
            }
        }
    } // namespace

    SocketCANDiscoverySource::SocketCANDiscoverySource()
        : SocketCANDiscoverySource("socketcan", default_channels, default_transport) {}

    SocketCANDiscoverySource::SocketCANDiscoverySource(ChannelEnumerator channel_enumerator,
                                                       TransportFactory transport_factory)
        : SocketCANDiscoverySource("socketcan", std::move(channel_enumerator), std::move(transport_factory)) {}

    SocketCANDiscoverySource::SocketCANDiscoverySource(std::string backend, ChannelEnumerator channel_enumerator,
                                                       TransportFactory transport_factory)
        : backend_(std::move(backend)), channel_enumerator_(std::move(channel_enumerator)),
          transport_factory_(std::move(transport_factory)) {
        if (backend_.empty()) {
            throw ValidationError("discovery backend name must not be empty");
        }
        if (!channel_enumerator_) {
            throw ValidationError("SocketCAN discovery requires a channel enumerator");
        }
        if (!transport_factory_) {
            throw ValidationError("SocketCAN discovery requires a transport factory");
        }
    }

    DiscoveryReport SocketCANDiscoverySource::scan(const DiscoveryConfig &config) {
        const auto started = Clock::now();
        const auto total_deadline = started + config.total_timeout;
        std::vector<DiscoveryFailure> failures;
        std::vector<DiscoveryChannel> available;
        try {
            available = channel_enumerator_();
        } catch (const std::exception &error) {
            record_failure(failures, backend_, "enumerate", error);
            return DiscoveryReport{{}, {}, std::move(failures), Clock::now() - started};
        }

        auto channels = selected_channels(available, config, backend_, failures);
        std::vector<DiscoveryCandidate> candidates;
        if (Clock::now() >= total_deadline) {
            failures.emplace_back("scan", "timeout", "discovery total time budget exhausted during channel enumeration",
                                  std::nullopt, backend_, "enumerate", std::nullopt, "TimeoutError");
            return DiscoveryReport{std::move(channels), {}, std::move(failures), Clock::now() - started};
        }
        for (const auto &channel : channels) {
            if (Clock::now() >= total_deadline) {
                failures.emplace_back("scan", "timeout", "discovery total time budget exhausted", channel.name(),
                                      backend_, "scan", channel.native_name(), "TimeoutError");
                break;
            }
            const auto candidates_before = candidates.size();
            const auto failures_before = failures.size();
            const bool supports_classic = channel.supports_classic();
            const bool supports_fd = channel.supports_fd().value_or(channel.kind() == TransportKind::kCanFd);
            if (!supports_classic && !supports_fd) {
                failures.emplace_back("select_protocol", "unsupported_protocol",
                                      "discovery channel supports neither Classic CAN nor CAN FD", channel.name(),
                                      backend_, "select_protocol", channel.native_name(), "DiscoveryError");
                continue;
            }
            if (supports_classic) {
                scan_wire(channel, TransportKind::kCan, config, total_deadline, backend_, transport_factory_,
                          candidates, failures, probe_classic);
            }
            if (supports_fd && Clock::now() >= total_deadline) {
                failures.emplace_back("scan", "timeout", "discovery total time budget exhausted", channel.name(),
                                      backend_, "scan", channel.native_name(), "TimeoutError");
                break;
            }
            if (supports_fd) {
                scan_wire(channel, TransportKind::kCanFd, config, total_deadline, backend_, transport_factory_,
                          candidates, failures,
                          [](const auto &session, const auto &selected_channel, const auto &selected_config,
                             auto deadline, const auto &factory, auto &found) {
                              probe_l30(session, selected_channel, selected_config, deadline, factory, found);
                              if (Clock::now() < deadline) {
                                  probe_o30(session, selected_channel, selected_config, deadline, factory, found);
                              }
                          });
            }
            if (candidates.size() == candidates_before && failures.size() == failures_before) {
                failures.emplace_back("probe_identity", "no_response", "no hand identity response was received",
                                      channel.name(), backend_, "probe_identity", channel.native_name(),
                                      "DiscoveryError");
            }
        }
        return DiscoveryReport{std::move(channels), std::move(candidates), std::move(failures), Clock::now() - started};
    }
} // namespace linkerbot::hand

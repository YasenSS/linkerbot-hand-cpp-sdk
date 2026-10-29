#include <linkerbot/hand/adapter_transport.hpp>
#include <linkerbot/hand/discovery.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include <linkerbot/can/errors.hpp>
#include <linkerbot/can/vendor_canfd_transport.hpp>

namespace linkerbot::hand {
    namespace {
        using Clock = std::chrono::steady_clock;

        [[nodiscard]] bool contains_protocol(const std::vector<TransportKind> &protocols, TransportKind protocol) {
            return std::find(protocols.begin(), protocols.end(), protocol) != protocols.end();
        }

        [[nodiscard]] DiscoveryFailure failure(std::string backend, std::string stage, std::string code,
                                               std::string message, std::optional<std::string> channel = {},
                                               std::optional<std::string> error_type = {},
                                               std::optional<int> native_status = {}) {
            auto operation = "enumerate_" + backend + '_' + stage;
            return DiscoveryFailure{
                std::move(operation), std::move(code), std::move(message),    channel,      std::move(backend),
                std::move(stage),     channel,         std::move(error_type), native_status};
        }

        [[nodiscard]] DiscoveryFailure failure_from_error(const std::string &backend, const std::string &stage,
                                                          const std::exception &error,
                                                          std::optional<std::string> channel = {}) {
            std::optional<int> native_status;
            std::string code = stage;
            if (const auto *can_error = dynamic_cast<const can::CANError *>(&error)) {
                code = can::error_code_name(can_error->code());
                native_status = can_error->native_status();
            } else if (const auto *hand_error = dynamic_cast<const HandError *>(&error)) {
                code = error_code_name(hand_error->code());
            }
            return failure(backend, stage, std::move(code), error.what(), std::move(channel), typeid(error).name(),
                           native_status);
        }

        [[nodiscard]] DiscoveryEnumeration unavailable_enumeration(const std::string &backend,
                                                                   const std::string &message) {
            return DiscoveryEnumeration{
                {}, {failure(backend, "dependency", "backend_unavailable", message, {}, "UnsupportedOperationError")}};
        }

        [[nodiscard]] DiscoveryEnumeration
        normalize_python_can_channels(const std::string &backend, DiscoveryEnumeration enumeration,
                                      const std::vector<std::string> &selected_channels,
                                      const std::vector<TransportKind> &protocols, uint32_t nominal_bitrate,
                                      uint32_t data_bitrate) {
            std::vector<DiscoveryChannel> channels;
            std::vector<DiscoveryFailure> failures = enumeration.failures();
            std::vector<std::string> found_channels;
            for (const auto &channel : enumeration.channels()) {
                if (channel.backend() != backend) {
                    failures.push_back(failure(backend, "channel", "backend_mismatch",
                                               "enumerated channel belongs to another backend", channel.native_name(),
                                               "ValidationError"));
                    continue;
                }
                if (!selected_channels.empty() && std::find(selected_channels.begin(), selected_channels.end(),
                                                            channel.native_name()) == selected_channels.end()) {
                    continue;
                }
                found_channels.push_back(channel.native_name());

                const bool requested_classic = contains_protocol(protocols, TransportKind::kCan);
                const bool requested_fd = contains_protocol(protocols, TransportKind::kCanFd);
                const bool supports_fd = channel.supports_fd().value_or(channel.kind() == TransportKind::kCanFd);
                if (requested_classic && !channel.supports_classic()) {
                    failures.push_back(failure(backend, "select_protocol", "unsupported_protocol",
                                               "adapter does not support the requested Classic CAN protocol",
                                               channel.native_name(), "DiscoveryError"));
                }
                if (requested_fd && !supports_fd) {
                    failures.push_back(failure(backend, "select_protocol", "unsupported_protocol",
                                               "adapter does not support the requested CAN FD protocol",
                                               channel.native_name(), "DiscoveryError"));
                }
                const bool classic = requested_classic && channel.supports_classic();
                const bool fd = requested_fd && supports_fd;
                if (!classic && !fd) {
                    continue;
                }

                auto unified_name = channel.name();
                const auto expected_prefix = backend + ':';
                if (unified_name.compare(0, expected_prefix.size(), expected_prefix) != 0) {
                    unified_name = expected_prefix + channel.native_name();
                }
                channels.emplace_back(backend, std::move(unified_name), channel.native_name(),
                                      fd ? TransportKind::kCanFd : TransportKind::kCan, nominal_bitrate,
                                      fd ? std::optional<uint32_t>{data_bitrate} : std::nullopt, classic,
                                      fd ? channel.supports_fd() : std::optional<bool>{false},
                                      channel.configuration_id());
            }
            for (const auto &requested : selected_channels) {
                if (std::find(found_channels.begin(), found_channels.end(), requested) == found_channels.end()) {
                    failures.push_back(failure(backend, "channel", "channel_not_found",
                                               "requested backend channel was not enumerated", requested,
                                               "DiscoveryError"));
                }
            }
            return DiscoveryEnumeration{std::move(channels), std::move(failures)};
        }

        [[nodiscard]] DiscoveryReport
        scan_adapter(const std::string &backend, const DiscoveryConfig &config,
                     const AdapterChannelEnumerator &enumerator, const AdapterTransportFactory &factory,
                     const std::function<DiscoveryEnumeration(DiscoveryEnumeration)> &normalize) {
            const auto started = Clock::now();
            const auto total_deadline = started + config.total_timeout;
            DiscoveryEnumeration enumeration;
            try {
                enumeration = normalize(enumerator(config));
            } catch (const std::exception &error) {
                return DiscoveryReport{
                    {}, {}, {failure_from_error(backend, "enumerate", error)}, Clock::now() - started};
            }

            if (Clock::now() >= total_deadline) {
                auto failures = enumeration.failures();
                failures.push_back(failure(backend, "scan", "timeout",
                                           "discovery total time budget exhausted during adapter enumeration", {},
                                           "TimeoutError"));
                return DiscoveryReport{enumeration.channels(), {}, std::move(failures), Clock::now() - started};
            }

            auto channels = std::make_shared<std::vector<DiscoveryChannel>>(enumeration.channels());
            DiscoveryConfig probe_config = config;
            probe_config.total_timeout =
                std::max(std::chrono::milliseconds{1},
                         std::chrono::duration_cast<std::chrono::milliseconds>(total_deadline - Clock::now()));
            SocketCANDiscoverySource probe{backend, [channels] { return *channels; }, factory};
            auto report = probe.scan(probe_config);
            auto failures = enumeration.failures();
            failures.insert(failures.end(), report.failures().begin(), report.failures().end());
            return DiscoveryReport{report.channels(), report.candidates(), std::move(failures), Clock::now() - started};
        }

        [[nodiscard]] std::unique_ptr<can::CANTransport> unsupported_transport(const std::string &backend,
                                                                               const std::string &channel) {
            throw UnsupportedOperationError(backend + " transport is not available for channel " + channel +
                                            "; inject an adapter transport factory or install the optional backend");
        }

        struct PcanBinding {
            PCANDiscoveryConfig options;
            std::shared_ptr<can::PcanApi> api;
            std::vector<can::PcanChannel> channels;
        };

        [[nodiscard]] DiscoveryEnumeration pcan_enumeration(const std::shared_ptr<PcanBinding> &binding) {
            std::optional<std::string> path;
            if (binding->options.library_path) {
                path = binding->options.library_path->u8string();
            }
            try {
                binding->api = can::load_pcan_api(path);
            } catch (const std::exception &error) {
                std::optional<int> native;
                if (const auto *typed = dynamic_cast<const can::CANError *>(&error)) {
                    native = typed->native_status();
                }
                return {
                    {},
                    {failure("pcan", "library", "backend_unavailable", error.what(), {}, "TransportIOError", native)}};
            }
            can::PcanEnumeration report;
            try {
                report = can::enumerate_pcan_channels(binding->api);
            } catch (const std::exception &error) {
                return {{}, {failure_from_error("pcan", "enumerate", error)}};
            }
            binding->channels = report.channels;
            std::vector<DiscoveryChannel> channels;
            std::vector<DiscoveryFailure> failures;
            const auto &options = binding->options;
            std::string configuration =
                binding->api->library_path + "|nominal=" + std::to_string(options.nominal_bitrate);
            if (contains_protocol(options.probe_protocols, TransportKind::kCanFd)) {
                configuration += "|" + options.fd_timing.bitrate_string(options.nominal_bitrate, options.data_bitrate);
            }
            for (const auto &problem : report.failures) {
                failures.push_back(failure("pcan", problem.stage, "transport_io", problem.message,
                                           problem.channel
                                               ? std::optional<std::string>(can::pcan_channel_name(*problem.channel))
                                               : std::nullopt,
                                           "PcanTransportError", problem.native_status));
            }
            for (const auto &channel : report.channels) {
                if (channel.owned_by_process || !(channel.condition & can::pcan_abi::available)) {
                    failures.push_back(
                        failure("pcan", "channel", "channel_busy",
                                "PCAN channel is occupied, unavailable or its condition is unknown (condition=" +
                                    std::to_string(channel.condition) + ")",
                                channel.native_name, "DiscoveryError"));
                    continue;
                }
                const bool fd = channel.supports_fd == true;
                channels.emplace_back("pcan", "pcan:" + channel.native_name, channel.native_name,
                                      fd ? TransportKind::kCanFd : TransportKind::kCan, options.nominal_bitrate,
                                      fd ? std::optional<uint32_t>(options.data_bitrate) : std::nullopt, true,
                                      channel.supports_fd, configuration);
            }
            return {std::move(channels), std::move(failures)};
        }

        [[nodiscard]] AdapterTransportFactory pcan_factory(const std::shared_ptr<PcanBinding> &binding) {
            return [binding](const std::string &native, TransportKind protocol) {
                const auto found = std::find_if(binding->channels.begin(), binding->channels.end(),
                                                [&](const auto &channel) { return channel.native_name == native; });
                if (!binding->api || found == binding->channels.end()) {
                    throw ValidationError("PCAN factory channel was not enumerated");
                }
                can::PcanConfig config;
                config.channel = found->handle;
                config.mode = protocol == TransportKind::kCanFd ? can::PcanMode::kFd : can::PcanMode::kClassic;
                config.nominal_bitrate = binding->options.nominal_bitrate;
                config.data_bitrate = binding->options.data_bitrate;
                config.fd_timing = binding->options.fd_timing;
                config.library_path = binding->api->library_path;
                return std::unique_ptr<can::CANTransport>(std::make_unique<can::PcanTransport>(config, binding->api));
            };
        }

        [[nodiscard]] DiscoveryEnumeration
        minicanfd_enumeration(const DiscoveryConfig &config,
                              const std::shared_ptr<MiniCANFDDiscoveryConfig> &captured = {}) {
            const auto &options = config.minicanfd;
            std::optional<std::string> path;
            if (options.library_path) {
                path = options.library_path->u8string();
            }
            std::shared_ptr<can::VendorCanFdApi> api;
            try {
                api = can::load_vendor_canfd_api(path);
            } catch (const std::exception &error) {
                return DiscoveryEnumeration{{}, {failure_from_error("minicanfd", "library", error)}};
            }
            // Capture the actual loaded module, including environment/default
            // selection, so later candidate opens cannot silently select another DLL.
            if (captured) {
                captured->library_path = std::filesystem::u8path(api->lifecycle_key);
            }
            std::vector<can::VendorCanFdChannel> selected;
            for (const auto &channel : options.channels) {
                selected.push_back({channel.device_index, channel.channel_index, {}});
            }
            can::VendorCanFdEnumeration enumeration;
            try {
                enumeration = can::enumerate_vendor_canfd_channels(api, selected);
            } catch (const std::exception &error) {
                return DiscoveryEnumeration{{}, {failure_from_error("minicanfd", "enumerate", error)}};
            }
            std::vector<DiscoveryChannel> channels;
            std::vector<DiscoveryFailure> failures;
            for (const auto &item : enumeration.channels) {
                const auto native = std::to_string(item.device_index) + ':' + std::to_string(item.channel_index);
                channels.emplace_back("minicanfd", "minicanfd:" + native, native, TransportKind::kCanFd,
                                      options.nominal_bitrate, options.data_bitrate, false, true,
                                      api->lifecycle_key + "|nominal=" + std::to_string(options.nominal_bitrate) +
                                          "|data=" + std::to_string(options.data_bitrate));
            }
            for (const auto &item : enumeration.failures) {
                std::optional<std::string> native;
                if (item.device_index) {
                    native = std::to_string(*item.device_index) + ':' + std::to_string(item.channel_index.value_or(0));
                }
                failures.push_back(failure("minicanfd", item.stage, item.code, item.message, native, "TransportIOError",
                                           item.native_status));
            }
            return DiscoveryEnumeration{std::move(channels), std::move(failures)};
        }

        [[nodiscard]] unsigned parse_index(const std::string &text, const char *name) {
            if (text.empty()) {
                throw ValidationError(std::string("MiniCANFD ") + name + " is empty");
            }
            size_t consumed = 0;
            const auto value = std::stoull(text, &consumed, 10);
            if (consumed != text.size() || value > std::numeric_limits<unsigned>::max()) {
                throw ValidationError(std::string("MiniCANFD ") + name + " is invalid");
            }
            return static_cast<unsigned>(value);
        }

        [[nodiscard]] AdapterTransportFactory minicanfd_factory(MiniCANFDDiscoveryConfig options) {
            return [options = std::move(options)](const std::string &native_name, TransportKind protocol) {
                if (protocol != TransportKind::kCanFd) {
                    throw ValidationError("MiniCANFD supports only CAN FD discovery probes");
                }
                const auto separator = native_name.find(':');
                if (separator == std::string::npos || native_name.find(':', separator + 1) != std::string::npos) {
                    throw ValidationError("MiniCANFD native channel must be device:channel");
                }
                can::VendorCanFdTransport::Config transport_config;
                transport_config.device_index = parse_index(native_name.substr(0, separator), "device index");
                transport_config.channel_index = parse_index(native_name.substr(separator + 1), "channel index");
                if (options.library_path) {
                    transport_config.library_path = options.library_path->u8string();
                }
                transport_config.nom_baud = options.nominal_bitrate;
                transport_config.dat_baud = options.data_bitrate;
                return std::unique_ptr<can::CANTransport>(can::make_vendor_canfd_transport(transport_config));
            };
        }
    } // namespace

    PCANDiscoverySource::PCANDiscoverySource() : use_default_transport_(true) {}

    PCANDiscoverySource::PCANDiscoverySource(AdapterChannelEnumerator channel_enumerator,
                                             AdapterTransportFactory transport_factory)
        : channel_enumerator_(std::move(channel_enumerator)), transport_factory_(std::move(transport_factory)) {
        if (!channel_enumerator_ || !transport_factory_) {
            throw ValidationError("PCAN discovery requires an enumerator and transport factory");
        }
    }

    DiscoveryReport PCANDiscoverySource::scan(const DiscoveryConfig &config) {
        auto enumerate = channel_enumerator_;
        auto factory = transport_factory_;
        if (use_default_transport_) {
            auto binding = std::make_shared<PcanBinding>();
            binding->options = config.pcan;
            enumerate = [binding](const DiscoveryConfig &) { return pcan_enumeration(binding); };
            factory = pcan_factory(binding);
        }
        return scan_adapter("pcan", config, enumerate, factory, [&config](DiscoveryEnumeration enumeration) {
            return normalize_python_can_channels("pcan", std::move(enumeration), config.pcan.channels,
                                                 config.pcan.probe_protocols, config.pcan.nominal_bitrate,
                                                 config.pcan.data_bitrate);
        });
    }

    namespace {
#if LINKERBOT_HAND_HAS_SLCAN
        can::SlcanConfig slcan_configuration(const SLCANDiscoveryConfig &options, const std::string &port,
                                             TransportKind wire) {
            if (wire != TransportKind::kCan && wire != TransportKind::kCanFd) {
                throw ValidationError("Invalid SLCAN wire protocol");
            }
            const bool canable = options.protocol == SLCANProtocol::kCanableFd;
            if (!canable && options.protocol != SLCANProtocol::kLawicelClassic) {
                throw ValidationError("Unknown SLCAN firmware profile");
            }
            if (!canable && wire == TransportKind::kCanFd) {
                throw ValidationError("FD requires the explicit CANable FD firmware profile");
            }
            can::SlcanConfig serial;
            serial.port = port;
            serial.serial_baud = options.serial_baud;
            serial.nominal_bitrate = options.nominal_bitrate;
            serial.command_timeout = options.command_timeout;
#if LINKERBOT_COMM_SLCAN_FD
            serial.protocol = canable ? can::SlcanProtocol::kCanableFd : can::SlcanProtocol::kLawicelClassic;
            serial.fd = wire == TransportKind::kCanFd;
            serial.data_bitrate = options.data_bitrate;
            serial.startup_delay = options.startup_delay;
#else
            if (canable || options.startup_delay.count()) {
                throw ValidationError("Paired comm has no SLCAN FD/Linux extension; update comm");
            }
#endif
            can::validate_slcan_config(serial);
            return serial;
        }
#endif
    }

    SLCANDiscoverySource::SLCANDiscoverySource() : use_default_transport_(true) {}

    SLCANDiscoverySource::SLCANDiscoverySource(AdapterChannelEnumerator channel_enumerator,
                                               AdapterTransportFactory transport_factory)
        : channel_enumerator_(std::move(channel_enumerator)), transport_factory_(std::move(transport_factory)) {
        if (!channel_enumerator_ || !transport_factory_) {
            throw ValidationError("SLCAN discovery requires an enumerator and transport factory");
        }
    }

    DiscoveryReport SLCANDiscoverySource::scan(const DiscoveryConfig &config) {
        auto enumerate = channel_enumerator_;
        auto factory = transport_factory_;
        if (use_default_transport_) {
            const auto options = config.slcan;
            enumerate = [options](const DiscoveryConfig &) {
                if (options.channels.empty()) {
                    return DiscoveryEnumeration{
                        {},
                        {failure("slcan", "allowlist", "allowlist_required",
                                 "SLCAN probes only explicit slcan.channels; no serial ports were opened")}};
                }
#if LINKERBOT_HAND_HAS_SLCAN
                if (options.probe_protocols.empty()) {
                    throw ValidationError("Select an SLCAN probe protocol");
                }
                // Validate every requested port/profile before touching metadata.
                for (const auto &name : options.channels) {
                    for (const auto wire : options.probe_protocols) {
                        (void)slcan_configuration(options, name, wire);
                    }
                }
                const auto ports = can::enumerate_slcan_ports(options.channels);
                std::vector<DiscoveryChannel> channels;
                for (const auto &port : ports) {
                    // Retain allowlist spelling for normalization/filtering;
                    // native CreateFile independently canonicalizes COM10+.
                    const auto original =
                        std::find_if(options.channels.begin(), options.channels.end(),
                                     [&](const auto &name) { return can::slcan_port_name(name) == port; });
                    const bool fd = options.protocol == SLCANProtocol::kCanableFd;
                    channels.emplace_back("slcan", "slcan:" + *original, *original,
                                          fd ? TransportKind::kCanFd : TransportKind::kCan, options.nominal_bitrate,
                                          fd ? std::optional<uint32_t>{options.data_bitrate} : std::nullopt, true, fd,
                                          std::string(fd ? "canable-fd|uart=" : "lawicel-classic|uart=") +
                                              std::to_string(options.serial_baud) +
                                              "|can=" + std::to_string(options.nominal_bitrate) +
                                              "|data=" + std::to_string(options.data_bitrate) +
                                              "|command_ms=" + std::to_string(options.command_timeout.count()) +
                                              "|startup_ms=" + std::to_string(options.startup_delay.count()));
                }
                return DiscoveryEnumeration{std::move(channels)};
#else
                return unavailable_enumeration(
                    "slcan", "Paired comm package has no SLCAN backend; install matching stage-B comm");
#endif
            };
            factory = [options](const std::string &port, TransportKind protocol) {
#if LINKERBOT_HAND_HAS_SLCAN
                if (std::find(options.channels.begin(), options.channels.end(), port) == options.channels.end()) {
                    throw ValidationError("SLCAN channel is outside the captured allowlist");
                }
                return std::unique_ptr<can::CANTransport>(
                    std::make_unique<can::SlcanTransport>(slcan_configuration(options, port, protocol)));
#else
                (void)protocol;
                return std::unique_ptr<can::CANTransport>(unsupported_transport("slcan", port));
#endif
            };
        }
        return scan_adapter("slcan", config, enumerate, factory, [&config](DiscoveryEnumeration enumeration) {
            return normalize_python_can_channels("slcan", std::move(enumeration), config.slcan.channels,
                                                 config.slcan.probe_protocols, config.slcan.nominal_bitrate,
                                                 config.slcan.data_bitrate);
        });
    }

    MiniCANFDDiscoverySource::MiniCANFDDiscoverySource()
        : channel_enumerator_([](const DiscoveryConfig &config) { return minicanfd_enumeration(config); }),
          use_default_transport_(true) {}

    MiniCANFDDiscoverySource::MiniCANFDDiscoverySource(AdapterChannelEnumerator channel_enumerator,
                                                       AdapterTransportFactory transport_factory)
        : channel_enumerator_(std::move(channel_enumerator)), transport_factory_(std::move(transport_factory)) {
        if (!channel_enumerator_ || !transport_factory_) {
            throw ValidationError("MiniCANFD discovery requires an enumerator and transport factory");
        }
    }

    DiscoveryReport MiniCANFDDiscoverySource::scan(const DiscoveryConfig &config) {
        auto enumerate = channel_enumerator_;
        auto factory = transport_factory_;
        if (use_default_transport_) {
            auto captured = std::make_shared<MiniCANFDDiscoveryConfig>(config.minicanfd);
            enumerate = [captured](const DiscoveryConfig &options) { return minicanfd_enumeration(options, captured); };
            factory = [captured](const std::string &native, TransportKind protocol) {
                return minicanfd_factory(*captured)(native, protocol);
            };
        }
        return scan_adapter("minicanfd", config, enumerate, factory, [&config](DiscoveryEnumeration enumeration) {
            std::vector<DiscoveryChannel> channels;
            auto failures = enumeration.failures();
            for (const auto &channel : enumeration.channels()) {
                if (channel.backend() != "minicanfd") {
                    failures.push_back(failure("minicanfd", "channel", "backend_mismatch",
                                               "enumerated channel belongs to another backend", channel.native_name(),
                                               "ValidationError"));
                    continue;
                }
                if (!config.minicanfd.channels.empty()) {
                    const auto native = channel.native_name();
                    const auto selected =
                        std::any_of(config.minicanfd.channels.begin(), config.minicanfd.channels.end(),
                                    [&native](const MiniCANFDChannel &item) {
                                        return native == std::to_string(item.device_index) + ':' +
                                                             std::to_string(item.channel_index);
                                    });
                    if (!selected) {
                        continue;
                    }
                }
                auto name = channel.name();
                const std::string prefix = "minicanfd:";
                if (name.compare(0, prefix.size(), prefix) != 0) {
                    name = "minicanfd:" + channel.native_name();
                }
                channels.emplace_back("minicanfd", std::move(name), channel.native_name(), TransportKind::kCanFd,
                                      config.minicanfd.nominal_bitrate, config.minicanfd.data_bitrate, false, true,
                                      channel.configuration_id());
            }
            return DiscoveryEnumeration{std::move(channels), std::move(failures)};
        });
    }
} // namespace linkerbot::hand

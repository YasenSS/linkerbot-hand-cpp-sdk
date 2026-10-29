#include <cstdlib>
#include <iostream>
#include <linkerbot/hand/adapter_transport.hpp>
#include <linkerbot/hand/discovery.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hand = linkerbot::hand;
namespace can = linkerbot::can;
namespace {
    const char *help = R"(windows_readonly --list|--discover|--read|--manual --backend pcan|minicanfd|slcan [options]
  No arguments / --help: show help without loading a driver or sending frames.
  --list: enumerate adapter metadata only; no channel open or CAN frames.
  --discover: identity-only discovery on the explicitly selected channel.
  --read: discover one hand, open, read identity/angles, then close.
  --manual: use --model and --side to connect/read without hand discovery.
  --channel PCAN_USBBUS1|0x51|device:channel|COM10|/dev/ttyACM0 (SLCAN requires an allowed port)
  --serial-baud 115200 --serial-timeout-ms 500 --serial-startup-ms 0
  --slcan-protocol lawicel-classic|canable-fd (explicit firmware profile)
  --mode classic|fd  --library <UTF-8 path>  --bitrate 1000000 --data-bitrate 5000000
  --model l6|o6|l20lite|l20|l30|o30 --side left|right --address <CAN/node/HOP ID>
  --fd-clock 80000000 --nom-brp 1 --nom-tseg1 63 --nom-tseg2 16 --nom-sjw 16
  --data-brp 1 --data-tseg1 11 --data-tseg2 4 --data-sjw 4
  PCAN FD defaults are an engineering 1/5 Mbit/s profile, requiring hardware validation.
  O30 uses standard FD without BRS; L30 uses extended FD. No motion commands exist here.
  SLCAN: Windows COM or Linux /dev/...; CANable FD uses d/D/b/B and Y2/Y5 (2M/5M).
  CANable has no serial ACK: a write completes at serial-driver acceptance only.
  Vendor drivers/DLLs are installed separately. Windows hardware acceptance is pending.
)";
    uint32_t number(const std::string &text) {
        if (text.empty() || text[0] == '-' || text[0] == '+' || text.find_first_of(" \t\r\n") != std::string::npos) {
            throw std::runtime_error("Invalid integer: " + text);
        }
        size_t used = 0;
        const auto value = std::stoull(text, &used, 0);
        if (used != text.size() || value > 0xffffffffULL) {
            throw std::runtime_error("Integer out of range: " + text);
        }
        return static_cast<uint32_t>(value);
    }
    hand::Model model(const std::string &text) {
        const std::map<std::string, hand::Model> models{{"l6", hand::Model::kL6},           {"o6", hand::Model::kO6},
                                                        {"l20lite", hand::Model::kL20Lite}, {"l20", hand::Model::kL20},
                                                        {"l30", hand::Model::kL30},         {"o30", hand::Model::kO30}};
        const auto found = models.find(text);
        if (found == models.end()) {
            throw std::runtime_error("Unknown model");
        }
        return found->second;
    }
    std::pair<unsigned, unsigned> mini_channel(const std::string &name) {
        const auto separator = name.find(':');
        if (separator == std::string::npos || name.find(':', separator + 1) != std::string::npos) {
            throw std::runtime_error("MiniCANFD channel must be device:channel");
        }
        return {number(name.substr(0, separator)), number(name.substr(separator + 1))};
    }
    void read_hand(hand::HandDevice &device) {
        try {
            device.open();
            const auto identity = device.get_device_info();
            std::cout << "identity model=" << int(identity.model) << " side=" << int(identity.side)
                      << " product=" << identity.product_code.value_or("unreported")
                      << " firmware=" << identity.firmware_version.value_or("unreported") << '\n';
            const auto angles = device.get_angles();
            if (!angles.meta.valid) {
                throw std::runtime_error("Angle response is invalid");
            }
            std::cout << "angles:";
            for (const auto angle : angles.raw) {
                std::cout << ' ' << angle;
            }
            std::cout << '\n';
            device.close();
        } catch (...) {
            try {
                device.close();
            } catch (const std::exception &error) {
                std::cerr << "cleanup: " << error.what() << '\n';
            }
            throw;
        }
    }
    int run(int argc, char **argv) {
        if (argc == 1) {
            std::cout << help;
            return 0;
        }
        std::map<std::string, std::string> options;
        const std::set<std::string> actions{"--list", "--discover", "--read", "--manual", "--help"};
        const std::set<std::string> values{
            "--backend",           "--channel",       "--mode",      "--library",     "--bitrate",
            "--data-bitrate",      "--model",         "--side",      "--address",     "--fd-clock",
            "--nom-brp",           "--nom-tseg1",     "--nom-tseg2", "--nom-sjw",     "--data-brp",
            "--data-tseg1",        "--data-tseg2",    "--data-sjw",  "--serial-baud", "--serial-timeout-ms",
            "--serial-startup-ms", "--slcan-protocol"};
        for (int i = 1; i < argc; ++i) {
            std::string key = argv[i];
            if (options.count(key)) {
                throw std::runtime_error("Duplicate argument: " + key);
            }
            if (actions.count(key)) {
                options[key] = "1";
            } else if (values.count(key) && i + 1 < argc) {
                options[key] = argv[++i];
            } else {
                throw std::runtime_error("Unknown argument or missing value: " + key);
            }
        }
        if (options.count("--help")) {
            std::cout << help;
            return 0;
        }
        unsigned action_count = 0;
        for (const auto &action : actions) {
            action_count += unsigned(options.count(action));
        }
        if (action_count != 1) {
            throw std::runtime_error("Select exactly one action");
        }
        auto required = [&](const char *key) {
            const auto found = options.find(key);
            if (found == options.end() || found->second.empty()) {
                throw std::runtime_error(std::string("Missing ") + key);
            }
            return found->second;
        };
        const auto backend = required("--backend");
        if (backend != "pcan" && backend != "minicanfd" && backend != "slcan") {
            throw std::runtime_error("Backend must be pcan, minicanfd or slcan");
        }
        const auto mode = options.count("--mode") ? options.at("--mode") : backend == "minicanfd" ? "fd" : "classic";
        if (mode != "classic" && mode != "fd") {
            throw std::runtime_error("Mode must be classic or fd");
        }
        if (backend == "minicanfd" && mode != "fd") {
            throw std::runtime_error("MiniCANFD supports FD only");
        }
        const auto slcan_profile =
            options.count("--slcan-protocol") ? options.at("--slcan-protocol") : "lawicel-classic";
        if (backend == "slcan") {
            if (slcan_profile != "lawicel-classic" && slcan_profile != "canable-fd") {
                throw std::runtime_error("Unknown SLCAN firmware profile");
            }
            if (mode == "fd" && slcan_profile != "canable-fd") {
                throw std::runtime_error("SLCAN FD requires --slcan-protocol canable-fd");
            }
            if (mode == "classic" && options.count("--data-bitrate")) {
                throw std::runtime_error("SLCAN Classic mode has no data bitrate");
            }
            for (const auto *key : {"--library", "--fd-clock", "--nom-brp", "--nom-tseg1", "--nom-tseg2", "--nom-sjw",
                                    "--data-brp", "--data-tseg1", "--data-tseg2", "--data-sjw"}) {
                if (options.count(key)) {
                    throw std::runtime_error(std::string("SLCAN does not accept ") + key);
                }
            }
#if !LINKERBOT_HAND_HAS_SLCAN
            throw std::runtime_error("Paired comm has no SLCAN backend; install matching stage-B comm");
#endif
        } else if (options.count("--serial-baud") || options.count("--serial-timeout-ms") ||
                   options.count("--serial-startup-ms") || options.count("--slcan-protocol")) {
            throw std::runtime_error("Serial options require SLCAN");
        }
        can::PcanConfig pcan;
        pcan.mode = mode == "fd" ? can::PcanMode::kFd : can::PcanMode::kClassic;
        if (options.count("--library")) {
            pcan.library_path = required("--library");
        }
        const auto set = [&](const char *key, uint32_t &field) {
            if (options.count(key)) {
                field = number(options.at(key));
            }
        };
        set("--bitrate", pcan.nominal_bitrate);
        set("--data-bitrate", pcan.data_bitrate);
        set("--fd-clock", pcan.fd_timing.clock_hz);
        set("--nom-brp", pcan.fd_timing.nominal_prescaler);
        set("--nom-tseg1", pcan.fd_timing.nominal_tseg1);
        set("--nom-tseg2", pcan.fd_timing.nominal_tseg2);
        set("--nom-sjw", pcan.fd_timing.nominal_sjw);
        set("--data-brp", pcan.fd_timing.data_prescaler);
        set("--data-tseg1", pcan.fd_timing.data_tseg1);
        set("--data-tseg2", pcan.fd_timing.data_tseg2);
        set("--data-sjw", pcan.fd_timing.data_sjw);
        if (!pcan.nominal_bitrate || !pcan.data_bitrate) {
            throw std::runtime_error("Bitrates must be positive");
        }
#if LINKERBOT_HAND_HAS_SLCAN
        can::SlcanConfig slcan;
        if (backend == "slcan") {
            slcan.port = can::slcan_port_name(required("--channel"));
            slcan.nominal_bitrate = pcan.nominal_bitrate;
            set("--serial-baud", slcan.serial_baud);
            if (options.count("--serial-timeout-ms")) {
                slcan.command_timeout = std::chrono::milliseconds{number(options.at("--serial-timeout-ms"))};
            }
#if LINKERBOT_COMM_SLCAN_FD
            slcan.protocol =
                slcan_profile == "canable-fd" ? can::SlcanProtocol::kCanableFd : can::SlcanProtocol::kLawicelClassic;
            slcan.fd = mode == "fd";
            slcan.data_bitrate = pcan.data_bitrate;
            if (options.count("--serial-startup-ms")) {
                slcan.startup_delay = std::chrono::milliseconds{number(options.at("--serial-startup-ms"))};
            }
#else
            if (slcan_profile != "lawicel-classic" || options.count("--serial-startup-ms")) {
                throw std::runtime_error("Update paired comm for SLCAN FD/Linux extension");
            }
#endif
            can::validate_slcan_config(slcan);
        }
#endif
        hand::HandSide side = hand::HandSide::kUnknown;
        if (options.count("--side")) {
            const auto value = required("--side");
            if (value != "left" && value != "right") {
                throw std::runtime_error("Side must be left or right");
            }
            side = value == "left" ? hand::HandSide::kLeft : hand::HandSide::kRight;
        }
        if (options.count("--list")) {
#if LINKERBOT_HAND_HAS_SLCAN
            if (backend == "slcan") {
                const auto ports = can::enumerate_slcan_ports({slcan.port});
                for (const auto &port : ports) {
                    std::cout << port << " requested-profile=" << slcan_profile << " mode=" << mode
                              << " (firmware unverified)\n";
                }
                return ports.empty() ? 1 : 0;
            }
#endif
            if (backend == "pcan") {
                const auto report = can::enumerate_pcan_channels(pcan.library_path);
                for (const auto &channel : report.channels) {
                    std::cout << channel.native_name << " hardware=" << channel.hardware_name
                              << " FD=" << (channel.supports_fd ? (*channel.supports_fd ? "yes" : "no") : "unknown")
                              << " condition=" << channel.condition << " owned=" << channel.owned_by_process << '\n';
                }
                for (const auto &failure : report.failures) {
                    std::cerr << failure.stage << ": " << failure.message << '\n';
                }
                return report.channels.empty() && !report.failures.empty() ? 1 : 0;
            }
            std::vector<can::VendorCanFdChannel> selected;
            if (options.count("--channel")) {
                const auto indices = mini_channel(required("--channel"));
                selected.push_back({indices.first, indices.second, {}});
            }
            const auto report = can::enumerate_vendor_canfd_channels(pcan.library_path, selected);
            for (const auto &channel : report.channels) {
                std::cout << channel.device_index << ':' << channel.channel_index << " FD=yes " << channel.library_path
                          << '\n';
            }
            for (const auto &failure : report.failures) {
                std::cerr << failure.stage << ": " << failure.message << '\n';
            }
            return report.channels.empty() && !report.failures.empty() ? 1 : 0;
        }
        const auto channel = required("--channel");
        if (backend == "pcan") {
            pcan.channel = can::pcan_channel_handle(channel);
        }
        if (options.count("--manual")) {
            hand::HandDeviceConfig config;
            config.model = model(required("--model"));
            if (side == hand::HandSide::kUnknown) {
                throw std::runtime_error("Manual connection requires --side");
            }
            config.side = side;
            config.interface_name = backend + ':' + channel;
            if (options.count("--address")) {
                config.address = number(options.at("--address"));
            }
            if ((hand::declared_capabilities(config.model, side).transport.kind == hand::TransportKind::kCanFd) !=
                (mode == "fd")) {
                throw std::runtime_error("Mode does not match the model transport");
            }
            hand::HandDevice::TransportFactory factory;
            if (backend == "pcan") {
                factory = hand::make_pcan_transport_factory(pcan);
            }
#if LINKERBOT_HAND_HAS_SLCAN
            else if (backend == "slcan") {
                factory = hand::make_slcan_transport_factory(slcan);
            }
#endif
            else {
                const auto indices = mini_channel(channel);
                can::VendorCanFdTransport::Config mini;
                mini.device_index = indices.first;
                mini.channel_index = indices.second;
                mini.library_path = pcan.library_path;
                mini.nom_baud = pcan.nominal_bitrate;
                mini.dat_baud = pcan.data_bitrate;
                factory = hand::make_minicanfd_transport_factory(mini);
            }
            hand::HandDevice device(config, std::move(factory));
            read_hand(device);
            return 0;
        }
        hand::DiscoveryConfig config;
        config.channels = {channel};
        config.total_timeout = std::chrono::seconds(10);
        if (options.count("--address")) {
            const auto address = number(options.at("--address"));
            if (address > 0x3fe) {
                throw std::runtime_error("Discovery address exceeds HOP range");
            }
            config.l30_node_ids = {static_cast<uint8_t>(address >= 1 && address <= 31 ? address : 1)};
            config.o30_request_ids = {address};
        }
        std::unique_ptr<hand::DiscoverySource> source;
        if (backend == "pcan") {
            config.pcan.channels = {can::pcan_channel_name(pcan.channel)};
            config.channels = config.pcan.channels;
            config.pcan.probe_protocols = {mode == "fd" ? hand::TransportKind::kCanFd : hand::TransportKind::kCan};
            config.pcan.nominal_bitrate = pcan.nominal_bitrate;
            config.pcan.data_bitrate = pcan.data_bitrate;
            config.pcan.fd_timing = pcan.fd_timing;
            if (pcan.library_path) {
                config.pcan.library_path = std::filesystem::u8path(*pcan.library_path);
            }
            source = std::make_unique<hand::PCANDiscoverySource>();
        }
#if LINKERBOT_HAND_HAS_SLCAN
        else if (backend == "slcan") {
            config.slcan.channels = {slcan.port};
            config.channels = config.slcan.channels;
            config.slcan.probe_protocols = {mode == "fd" ? hand::TransportKind::kCanFd : hand::TransportKind::kCan};
            config.slcan.protocol =
                slcan_profile == "canable-fd" ? hand::SLCANProtocol::kCanableFd : hand::SLCANProtocol::kLawicelClassic;
            config.slcan.data_bitrate = pcan.data_bitrate;
            if (options.count("--serial-startup-ms")) {
                config.slcan.startup_delay = std::chrono::milliseconds{number(options.at("--serial-startup-ms"))};
            }
            config.slcan.nominal_bitrate = slcan.nominal_bitrate;
            config.slcan.serial_baud = slcan.serial_baud;
            config.slcan.command_timeout = slcan.command_timeout;
            source = std::make_unique<hand::SLCANDiscoverySource>();
        }
#endif
        else {
            const auto indices = mini_channel(channel);
            config.minicanfd.channels = {{indices.first, indices.second}};
            config.minicanfd.nominal_bitrate = pcan.nominal_bitrate;
            config.minicanfd.data_bitrate = pcan.data_bitrate;
            if (pcan.library_path) {
                config.minicanfd.library_path = std::filesystem::u8path(*pcan.library_path);
            }
            source = std::make_unique<hand::MiniCANFDDiscoverySource>();
        }
        const auto report = hand::discover_hands(config, *source);
        for (const auto &failure : report.failures()) {
            std::cerr << failure.backend().value_or("discovery") << ' ' << failure.code() << ": " << failure.message()
                      << '\n';
        }
        for (const auto &candidate : report.candidates()) {
            std::cout << candidate.channel().name() << " model=" << int(candidate.model())
                      << " side=" << int(candidate.side()) << " status=" << int(candidate.status()) << '\n';
        }
        if (!options.count("--read")) {
            return report.candidates().empty() ? 1 : 0;
        }
        const auto desired_model = options.count("--model") ? model(options.at("--model")) : hand::Model::kUnknown;
        const hand::DiscoveryCandidate *selected = nullptr;
        for (const auto &candidate : report.candidates()) {
            if (desired_model != hand::Model::kUnknown && candidate.model() != desired_model) {
                continue;
            }
            if (side != hand::HandSide::kUnknown && candidate.side() != hand::HandSide::kUnknown &&
                candidate.side() != side) {
                continue;
            }
            if (options.count("--address")) {
                const auto actual = candidate.address().node_id() ? uint32_t(*candidate.address().node_id())
                                                                  : candidate.address().request_id();
                if (actual != number(options.at("--address"))) {
                    continue;
                }
            }
            if (selected) {
                throw std::runtime_error("Multiple matching hands; specify model, side or address");
            }
            selected = &candidate;
        }
        if (!selected) {
            throw std::runtime_error("No matching hand found");
        }
        auto device = side == hand::HandSide::kUnknown ? selected->create_hand_device()
                                                       : selected->create_hand_device(hand::HandRuntimeConfig{}, side);
        read_hand(*device);
        return 0;
    }
}
#if defined(_WIN32)
int wmain(int argc, wchar_t **argv) {
    try {
        std::vector<std::string> encoded;
        for (int i = 0; i < argc; ++i) {
            const int size =
                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
            if (!size) {
                throw std::runtime_error("Invalid Unicode command line");
            }
            std::string value(size, '\0');
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, value.data(), size, nullptr,
                                     nullptr)) {
                throw std::runtime_error("Command line encoding failed");
            }
            value.resize(size - 1);
            encoded.push_back(std::move(value));
        }
        std::vector<char *> arguments;
        for (auto &value : encoded) {
            arguments.push_back(value.data());
        }
        return run(argc, arguments.data());
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
#endif

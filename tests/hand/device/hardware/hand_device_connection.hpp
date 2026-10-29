#pragma once

#include <linkerbot/hand/adapter_transport.hpp>
#ifdef __linux__
#include <linkerbot/can/socketcan_transport.hpp>
#endif
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Only environment/encoding APIs are needed here.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif
#endif

namespace linkerbot::hand::hardware_test {
    inline std::optional<std::string> env(std::string_view name) {
#ifdef _WIN32
        // All keys are ASCII; values such as DLL and evidence paths may not be.
        const auto key = std::wstring{name.begin(), name.end()};
        const auto count = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
        if (!count) {
            return std::nullopt;
        }
        std::wstring value(count, L'\0');
        const auto copied = GetEnvironmentVariableW(key.c_str(), value.data(), count);
        if (!copied || copied >= count) {
            throw std::runtime_error("Hardware environment changed while reading");
        }
        const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(copied),
                                              nullptr, 0, nullptr, nullptr);
        if (!size) {
            throw std::runtime_error("Invalid hardware environment encoding");
        }
        std::string utf8(size, '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(copied), utf8.data(),
                                 size, nullptr, nullptr)) {
            throw std::runtime_error("Hardware environment conversion failed");
        }
        return utf8;
#else
        const auto key = std::string{name};
        const auto *value = std::getenv(key.c_str());
        if (!value || !*value) {
            return std::nullopt;
        }
        return std::string{value};
#endif
    }

    inline std::optional<uint32_t> parse_u32(const std::optional<std::string> &value) {
        if (!value) {
            return std::nullopt;
        }
        if (value->empty() || value->find_first_of("+- \t\r\n") != std::string::npos) {
            throw std::invalid_argument("Invalid unsigned integer: " + *value);
        }
        size_t consumed = 0;
        const auto parsed = std::stoull(*value, &consumed, 0);
        if (consumed != value->size() || parsed > std::numeric_limits<uint32_t>::max()) {
            throw std::out_of_range("Unsigned integer out of range: " + *value);
        }
        return static_cast<uint32_t>(parsed);
    }

    using Environment = std::function<std::optional<std::string>(std::string_view)>;

    // Pure configuration: constructing a factory never loads a driver or opens a channel.
    inline HandDevice::TransportFactory hardware_factory(const HandDeviceConfig &config, Environment read = env) {
        const auto backend = read("HAND_DEVICE_BACKEND").value_or("socketcan");
        const bool fd = declared_capabilities(config.model, config.side).transport.kind == TransportKind::kCanFd;
        if (backend == "socketcan") {
#ifdef __linux__
            return [config, fd] { return std::make_unique<can::SocketCANTransport>(config.interface_name, fd); };
#else
            throw std::invalid_argument("Windows hardware tests require HAND_DEVICE_BACKEND=pcan, minicanfd or slcan");
#endif
        }
        if (backend != "pcan" && backend != "minicanfd" && backend != "slcan") {
            throw std::invalid_argument("Unknown HAND_DEVICE_BACKEND");
        }
        const auto channel = read("HAND_DEVICE_CHANNEL");
        const auto library = read("HAND_DEVICE_LIBRARY");
        if (!channel || channel->empty()) {
            throw std::invalid_argument("Adapter tests require explicit HAND_DEVICE_CHANNEL");
        }
        const auto required_number = [&](const char *key) {
            const auto value = parse_u32(read(key));
            if (!value || !*value) {
                throw std::invalid_argument(std::string("Missing positive ") + key);
            }
            return *value;
        };
        const auto nominal = required_number("HAND_DEVICE_BITRATE");
        if (backend == "slcan") {
#if LINKERBOT_HAND_HAS_SLCAN
            const auto profile = read("HAND_DEVICE_SLCAN_PROTOCOL").value_or("lawicel-classic");
            if (profile != "lawicel-classic" && profile != "canable-fd") {
                throw std::invalid_argument("SLCAN protocol must be lawicel-classic or canable-fd");
            }
            if (library || (!fd && read("HAND_DEVICE_DATA_BITRATE"))) {
                throw std::invalid_argument("SLCAN takes no DLL; data bitrate is for FD mode only");
            }
            if (fd && profile != "canable-fd") {
                throw std::invalid_argument("FD hands require explicit HAND_DEVICE_SLCAN_PROTOCOL=canable-fd");
            }
            can::SlcanConfig serial;
            serial.port = *channel;
            serial.nominal_bitrate = nominal;
            serial.serial_baud = required_number("HAND_DEVICE_SERIAL_BAUD");
            serial.command_timeout = std::chrono::milliseconds{required_number("HAND_DEVICE_SERIAL_TIMEOUT_MS")};
#if LINKERBOT_COMM_SLCAN_FD
            serial.protocol =
                profile == "canable-fd" ? can::SlcanProtocol::kCanableFd : can::SlcanProtocol::kLawicelClassic;
            serial.fd = fd;
            if (fd) {
                serial.data_bitrate = required_number("HAND_DEVICE_DATA_BITRATE");
            }
            if (const auto delay = parse_u32(read("HAND_DEVICE_SERIAL_STARTUP_MS"))) {
                serial.startup_delay = std::chrono::milliseconds{*delay};
            }
#else
            if (profile == "canable-fd" || read("HAND_DEVICE_SERIAL_STARTUP_MS")) {
                throw std::invalid_argument("Paired comm lacks SLCAN FD/Linux extension");
            }
#endif
            return make_slcan_transport_factory(serial);
#else
            throw std::invalid_argument("Paired comm package has no SLCAN backend");
#endif
        }
        if (!library || library->empty()) {
            throw std::invalid_argument("Adapter tests require explicit HAND_DEVICE_LIBRARY");
        }
        const auto data = fd ? required_number("HAND_DEVICE_DATA_BITRATE") : nominal;
        if (backend == "pcan") {
            can::PcanConfig pcan;
            pcan.channel = can::pcan_channel_handle(*channel);
            pcan.library_path = *library;
            pcan.mode = fd ? can::PcanMode::kFd : can::PcanMode::kClassic;
            pcan.nominal_bitrate = nominal;
            pcan.data_bitrate = data;
            if (fd) {
                // No implicit engineering timing profile during hardware acceptance.
                pcan.fd_timing.clock_hz = required_number("HAND_DEVICE_FD_CLOCK");
                pcan.fd_timing.nominal_prescaler = required_number("HAND_DEVICE_NOM_BRP");
                pcan.fd_timing.nominal_tseg1 = required_number("HAND_DEVICE_NOM_TSEG1");
                pcan.fd_timing.nominal_tseg2 = required_number("HAND_DEVICE_NOM_TSEG2");
                pcan.fd_timing.nominal_sjw = required_number("HAND_DEVICE_NOM_SJW");
                pcan.fd_timing.data_prescaler = required_number("HAND_DEVICE_DATA_BRP");
                pcan.fd_timing.data_tseg1 = required_number("HAND_DEVICE_DATA_TSEG1");
                pcan.fd_timing.data_tseg2 = required_number("HAND_DEVICE_DATA_TSEG2");
                pcan.fd_timing.data_sjw = required_number("HAND_DEVICE_DATA_SJW");
            }
            return make_pcan_transport_factory(pcan);
        }
        if (!fd) {
            throw std::invalid_argument("MiniCANFD hardware tests require an FD hand");
        }
        const auto separator = channel->find(':');
        if (separator == std::string::npos || channel->find(':', separator + 1) != std::string::npos) {
            throw std::invalid_argument("MiniCANFD channel must be device:channel");
        }
        can::VendorCanFdTransport::Config mini;
        mini.device_index = *parse_u32(channel->substr(0, separator));
        mini.channel_index = *parse_u32(channel->substr(separator + 1));
        mini.library_path = *library;
        mini.nom_baud = nominal;
        mini.dat_baud = data;
        return make_minicanfd_transport_factory(mini);
    }
}

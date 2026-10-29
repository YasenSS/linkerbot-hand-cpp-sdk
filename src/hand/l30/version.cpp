#include "linkerbot/hand/l30/version.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l30/version_manager.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace linkerbot::hand::l30 {

    namespace {
        // Offsets inside the 18-byte DeviceInfo structure (section 9.2.1).
        constexpr size_t kProductIdIndex = 0;
        constexpr size_t kSerialStart = 1;
        constexpr size_t kSoftwareStart = 5;
        constexpr size_t kHardwareStart = 8;
        constexpr size_t kStructureStart = 11;
        constexpr size_t kNodeIdIndex = 14;
        constexpr size_t kHandTypeIndex = 15;
        constexpr size_t kSensorTypeIndex = 16;
        constexpr size_t kOriginIndex = 17;

        void check_timeout(std::chrono::milliseconds timeout_ms) {
            if (timeout_ms.count() <= 0) {
                throw ValidationError("timeout_ms must be positive");
            }
        }

        Version decode_version(const std::vector<uint8_t> &payload, size_t start) {
            return Version{payload[start], payload[start + 1], payload[start + 2]};
        }

        HandSide decode_hand_side(uint8_t value) {
            if (value == kHandTypeLeft) {
                return HandSide::kLeft;
            }
            if (value == kHandTypeRight) {
                return HandSide::kRight;
            }
            throw protocol::ProtocolError("unknown L30 hand type " + std::to_string(static_cast<unsigned>(value)));
        }

        char decode_ascii_byte(uint8_t value) {
            if (value > 0x7FU) {
                throw protocol::ProtocolError("DeviceInfo byte is not valid ASCII");
            }
            return static_cast<char>(value);
        }
    } // namespace

    std::string_view hand_side_name(HandSide side) {
        switch (side) {
        case HandSide::kLeft:
            return "left";
        case HandSide::kRight:
            return "right";
        }
        throw ValidationError("unknown L30 hand side");
    }

    std::string Version::to_string() const {
        return "V" + std::to_string(static_cast<unsigned>(major)) + "." + std::to_string(static_cast<unsigned>(minor)) +
               "." + std::to_string(static_cast<unsigned>(patch));
    }

    DeviceInfo decode_device_info_response(const std::vector<uint8_t> &response) {
        const auto payload =
            protocol::decode_response_payload(response.data(), response.size(), protocol::kDeviceInfoBytes);

        DeviceInfo info{};
        info.product_id = payload[kProductIdIndex];
        info.serial_number = (static_cast<uint32_t>(payload[kSerialStart]) << 24U) |
                             (static_cast<uint32_t>(payload[kSerialStart + 1]) << 16U) |
                             (static_cast<uint32_t>(payload[kSerialStart + 2]) << 8U) |
                             static_cast<uint32_t>(payload[kSerialStart + 3]);
        info.software_version = decode_version(payload, kSoftwareStart);
        info.hardware_version = decode_version(payload, kHardwareStart);
        info.structure_version = decode_version(payload, kStructureStart);
        info.node_id = payload[kNodeIdIndex];
        info.hand_side = decode_hand_side(payload[kHandTypeIndex]);
        info.sensor_type = decode_ascii_byte(payload[kSensorTypeIndex]);
        info.origin = decode_ascii_byte(payload[kOriginIndex]);
        info.timestamp = std::chrono::steady_clock::now();
        return info;
    }

    void verify_device_info(const DeviceInfo &info, uint8_t expected_node_id, std::optional<IdentitySide> expected_side,
                            std::string_view product_code) {
        if (info.product_id != 0x13 || info.node_id != expected_node_id) {
            throw IdentityMismatchError("L30 product ID or node ID does not match endpoint");
        }

        const auto side = info.hand_side == HandSide::kLeft ? IdentitySide::kLeft : IdentitySide::kRight;
        if (expected_side && *expected_side != side) {
            throw IdentityMismatchError("L30 side does not match expected side");
        }

        const auto parts = identity_detail::split_product(product_code);
        if (parts.size() != 7 || parts[0] != "LHT30") {
            throw IdentityMismatchError("unexpected L30 product code: " + std::string(product_code));
        }

        const auto numeric = [](const std::string &value) {
            return !value.empty() &&
                   std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
        };
        if (!numeric(parts[1]) || !numeric(parts[2])) {
            throw IdentityProtocolError("L30 product version and serial must be numeric");
        }

        const auto first_digit = parts[2].find_first_not_of('0');
        const auto serial = first_digit == std::string::npos ? "0" : parts[2].substr(first_digit);
        if (serial != std::to_string(info.serial_number) || parts[3] != (side == IdentitySide::kLeft ? "L" : "R") ||
            parts[4] != std::string(1, info.sensor_type) || parts[5] != "3" ||
            parts[6] != std::string(1, info.origin)) {
            throw IdentityMismatchError("L30 product code disagrees with DeviceInfo");
        }
    }

    VersionManager::VersionManager(Client &client) : client_(&client) {}

    DeviceInfo VersionManager::get_device_info(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        check_timeout(timeout_ms);
        const auto response = client_->read(protocol::kParentConfig, protocol::kSubDeviceInfo, timeout_ms);
        return decode_device_info_response(response);
    }

    DeviceInfo VersionManager::get_verified_device_info(std::chrono::milliseconds timeout_ms,
                                                        std::optional<IdentitySide> expected_side) {
        const auto call = client_->enter();
        const auto operation = client_->operation(timeout_ms);
        auto info = get_device_info(operation.remaining());
        const auto code = get_product_code(operation.remaining());
        verify_device_info(info, client_->node_id(), expected_side, code);
        return info;
    }

    std::string VersionManager::get_product_code(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        check_timeout(timeout_ms);
        const auto response = client_->read(protocol::kParentConfig, protocol::kSubProductCode, timeout_ms);
        return protocol::decode_ascii_response(response.data(), response.size());
    }

    uint8_t VersionManager::get_node_id(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        check_timeout(timeout_ms);
        const auto response = client_->read(protocol::kParentConfig, protocol::kSubNodeId, timeout_ms);
        return protocol::decode_response_payload(response.data(), response.size(), 1)[0];
    }

    HandSide VersionManager::get_hand_side(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        check_timeout(timeout_ms);
        const auto response = client_->read(protocol::kParentConfig, protocol::kSubHandType, timeout_ms);
        const auto payload = protocol::decode_response_payload(response.data(), response.size(), 1);
        return decode_hand_side(payload[0]);
    }

} // namespace linkerbot::hand::l30

#include "linkerbot/hand/o30/version_manager.hpp"

#include <cstddef>
#include <regex>
#include <vector>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    namespace {
        std::string slice_ascii(const std::vector<uint8_t> &blob, size_t offset, size_t length) {
            if (blob.size() < offset + length) {
                throw protocol::ProtocolError("product-info blob is shorter than "
                                              "the documented 225-byte layout");
            }
            return protocol::decode_ascii({blob.begin() + static_cast<std::ptrdiff_t>(offset),
                                           blob.begin() + static_cast<std::ptrdiff_t>(offset + length)});
        }
    } // namespace

    VersionManager::VersionManager(Client &client) : client_(&client) {}

    bool product_info_uses_documented_layout(const std::vector<uint8_t> &deployed_blob) {
        if (deployed_blob.size() < protocol::kProductInfoBytes) {
            throw protocol::ProtocolError("product-info blob is shorter than the documented 225-byte layout");
        }
        const auto protocol_version = slice_ascii(deployed_blob, 0x61, 0x08);
        const auto board_probe = slice_ascii(deployed_blob, 0x71, 0x08);
        const auto version_probe = slice_ascii(deployed_blob, 0x99, 0x08);
        const bool deployed = std::regex_match(protocol_version, std::regex("[Vv]?0\\.0\\.3")) ||
                              std::regex_match(board_probe, std::regex("[VMvm]?[0-9]+(\\.[0-9]+){1,2}")) ||
                              std::regex_search(version_probe, std::regex("^[0-9]{4}-[0-9]{2}"));
        return !deployed;
    }

    ProductInfo decode_product_info_blob(const std::vector<uint8_t> &blob) {
        const bool documented = product_info_uses_documented_layout(blob);
        if (documented && blob.size() < protocol::kProductInfoBytes + 0x18U) {
            throw protocol::ProtocolError("short HOP 0.0.4 identity tail");
        }
        const bool deployed = !documented;

        ProductInfo info{};
        info.model = slice_ascii(blob, 0x00, 0x10);
        info.voltage_range = slice_ascii(blob, 0x10, 0x10);
        info.mcu_uid = slice_ascii(blob, 0x20, 0x19);
        info.device_uid = slice_ascii(blob, 0x39, 0x20);
        info.protocol_name = slice_ascii(blob, 0x59, 0x08);
        info.protocol_version = slice_ascii(blob, 0x61, 0x08);
        info.interface_hw_version = slice_ascii(blob, 0x69, deployed ? 8 : 16);
        info.adapter_hw_version = slice_ascii(blob, deployed ? 0x71 : 0x79, deployed ? 8 : 16);
        info.control_hw_version = slice_ascii(blob, deployed ? 0x79 : 0x89, deployed ? 8 : 16);
        info.bootloader_version = slice_ascii(blob, deployed ? 0x81 : 0x99, 8);
        info.app_version = slice_ascii(blob, deployed ? 0x89 : 0xA1, 8);
        info.mechanical_version = slice_ascii(blob, deployed ? 0x91 : 0xA9, 8);
        info.build_time = slice_ascii(blob, deployed ? 0x99 : 0xB1, 32);
        info.supported_interfaces = slice_ascii(blob, deployed ? 0xB9 : 0xD1, 32);
        info.hand_side = slice_ascii(blob, deployed ? 0xD9 : 0xF1, 8);
        info.timestamp = std::chrono::steady_clock::now();
        return info;
    }

    void verify_product_info(const ProductInfo &info, std::optional<IdentitySide> expected_side) {
        const auto parts = identity_detail::split_product(info.model);
        const auto model = identity_detail::upper(parts.at(0));
        if (model != "O30" && model != "O30I") {
            throw IdentityMismatchError("expected O30/O30i product model, got " + info.model);
        }
        if (identity_detail::upper(info.protocol_name) != "HOP" || info.protocol_version.empty() ||
            info.device_uid.empty() || info.app_version.empty()) {
            throw IdentityProtocolError("O30 requires HOP protocol, version, device UID and app version");
        }

        auto reported = identity_detail::parse_side(info.hand_side);
        std::optional<IdentitySide> product_side;
        if (parts.size() >= 4) {
            const auto label = identity_detail::upper(parts[3]);
            if (label == "L" || label == "R") {
                product_side = identity_detail::parse_side(label);
            }
        }
        if (reported && product_side && reported != product_side) {
            throw IdentityMismatchError("O30 product model and reported side disagree");
        }
        const auto actual = reported ? reported : product_side;
        if (expected_side && !actual) {
            throw IdentityProtocolError("O30 side cannot be verified: no reported side");
        }
        if (expected_side && expected_side != actual) {
            throw IdentityMismatchError("O30 side does not match expected side");
        }
    }

    std::string VersionManager::read_ascii(uint8_t si, uint8_t edl, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        const auto bytes = client_->read(protocol::kMiProductInfo, si, edl, false, timeout_ms);
        if (bytes.size() != edl) {
            throw protocol::ProtocolError("short product-info ASCII field");
        }
        return protocol::decode_ascii(bytes);
    }

    ProductInfo VersionManager::get_product_info(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        auto blob = client_->read(protocol::kMiProductInfo, 0, protocol::kProductInfoBytes, false, timeout_ms);
        if (blob.size() != protocol::kProductInfoBytes) {
            throw protocol::ProtocolError("short HOP identity object");
        }
        // Match Python identity._select_layout: version alone is insufficient;
        // some 0.0.4 devices still expose the deployed 0.0.3 field layout.
        if (product_info_uses_documented_layout(blob)) {
            const auto tail = client_->read(protocol::kMiProductInfo, 0xE1, 0x18, false, timeout_ms);
            if (tail.size() != 0x18) {
                throw protocol::ProtocolError("short HOP 0.0.4 identity tail");
            }
            blob.insert(blob.end(), tail.begin(), tail.end());
        }
        auto info = decode_product_info_blob(blob);
        info.timestamp = client_->received_at();
        return info;
    }

    ProductInfo VersionManager::get_verified_product_info(std::chrono::milliseconds timeout_ms,
                                                          std::optional<IdentitySide> expected_side) {
        auto operation = client_->operation(timeout_ms);
        auto info = get_product_info(timeout_ms);
        verify_product_info(info, expected_side);
        return info;
    }

    std::string VersionManager::get_model(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        return read_ascii(0x00, 0x10, timeout_ms);
    }

    std::string VersionManager::get_protocol_name(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        return read_ascii(0x59, 0x08, timeout_ms);
    }

    std::string VersionManager::get_protocol_version(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        return read_ascii(0x61, 0x08, timeout_ms);
    }

    std::string VersionManager::get_device_uid(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        return read_ascii(0x39, 0x20, timeout_ms);
    }

} // namespace linkerbot::hand::o30

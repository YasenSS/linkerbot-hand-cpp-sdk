#pragma once

#include <linkerbot/hand/identity.hpp>

#include <chrono>
#include <cstdint>
#include <string>

#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/l30/version.hpp>

namespace linkerbot::hand::l30 {

    // Read-only device identity queries. None of these need a config unlock.
    class VersionManager {
        friend class L30;

    public:
        explicit VersionManager(Client &client);

        VersionManager(const VersionManager &) = delete;
        VersionManager &operator=(const VersionManager &) = delete;
        VersionManager(VersionManager &&) = delete;
        VersionManager &operator=(VersionManager &&) = delete;

        [[nodiscard]] DeviceInfo get_device_info(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                     1000});

        // ASCII production code, e.g. "LHT30-06-001-L-B-3-A".
        [[nodiscard]] std::string get_product_code(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                       1000});

        [[nodiscard]] uint8_t get_node_id(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{1000});

        [[nodiscard]] HandSide get_hand_side(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{1000});

        [[nodiscard]] DeviceInfo
        get_verified_device_info(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{1000},
                                 std::optional<IdentitySide> expected_side = {});

    private:
        Client *client_;
    };

} // namespace linkerbot::hand::l30

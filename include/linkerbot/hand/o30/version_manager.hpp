#pragma once

#include <linkerbot/hand/identity.hpp>

#include <chrono>
#include <cstdint>
#include <string>

#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/version.hpp>

namespace linkerbot::hand::o30 {

    class VersionManager {
        friend class O30;

    public:
        explicit VersionManager(Client &client);

        VersionManager(const VersionManager &) = delete;
        VersionManager &operator=(const VersionManager &) = delete;
        VersionManager(VersionManager &&) = delete;
        VersionManager &operator=(VersionManager &&) = delete;

        [[nodiscard]] ProductInfo get_product_info(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                       1000});

        [[nodiscard]] std::string get_model(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});
        [[nodiscard]] std::string get_protocol_name(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                        200});
        [[nodiscard]] std::string get_protocol_version(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{
                                                           200});
        [[nodiscard]] std::string get_device_uid(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

        [[nodiscard]] ProductInfo
        get_verified_product_info(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{1000},
                                  std::optional<IdentitySide> expected_side = {});

    private:
        [[nodiscard]] std::string read_ascii(uint8_t si, uint8_t edl, std::chrono::milliseconds timeout_ms);

        Client *client_;
    };

} // namespace linkerbot::hand::o30

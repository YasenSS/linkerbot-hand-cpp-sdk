#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l6/l6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l6::L6;
using linkerbot::hand::l6::Side;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kSnCmd = 0xC0;
    constexpr uint8_t kPcbCmd = 0xC1;
    constexpr uint8_t kFwCmd = 0xC2;
    constexpr uint8_t kMechCmd = 0xC4;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    Message sn_frame(uint32_t id, uint8_t key, const char *six) {
        Message msg;
        msg.id = id;
        msg.dlc = 8;
        msg.data[0] = kSnCmd;
        msg.data[1] = key;
        for (uint8_t i = 0; i < 6; ++i) {
            msg.data[2 + i] = static_cast<uint8_t>(six[i]);
        }
        return msg;
    }

    FakeTransport::Handler version_handler(uint32_t id, const std::vector<uint8_t> &sn_keys) {
        return [id, sn_keys](const Message &req) {
            if (req.dlc < 1) {
                return std::vector<Message>{};
            }
            switch (req.data[0]) {
            case kSnCmd: {
                // "LHL6-03-105-L-B-1" laid out as 3×6 (pad last with spaces then
                // trimmed only if zeros; use the on-wire ASCII from real hardware).
                if (sn_keys.size() == 3 && sn_keys[0] == 0 && sn_keys[1] == 6) {
                    return std::vector<Message>{sn_frame(id, 0, "LHL6 -"), sn_frame(id, 6, "03-105"),
                                                sn_frame(id, 12, "-L-B-1")};
                }
                return std::vector<Message>{sn_frame(id, 0, "LHL6-0"), sn_frame(id, 1, "3-001-"),
                                            sn_frame(id, 2, "L-Z-1-"), sn_frame(id, 3, "A\0\0\0\0\0")};
            }
            case kPcbCmd: {
                Message reply;
                reply.id = id;
                reply.dlc = 5;
                reply.data[0] = kPcbCmd;
                reply.data[1] = 0x01;
                reply.data[2] = 3;
                reply.data[3] = 0;
                reply.data[4] = 0;
                return std::vector<Message>{reply};
            }
            case kFwCmd: {
                Message reply;
                reply.id = id;
                reply.dlc = 4;
                reply.data[0] = kFwCmd;
                reply.data[1] = 2;
                reply.data[2] = 3;
                reply.data[3] = 3;
                return std::vector<Message>{reply};
            }
            case kMechCmd: {
                Message reply;
                reply.id = id;
                reply.dlc = 4;
                reply.data[0] = kMechCmd;
                reply.data[1] = 3;
                reply.data[2] = 1;
                reply.data[3] = 0;
                return std::vector<Message>{reply};
            }
            default:
                return std::vector<Message>{};
            }
        };
    }

} // namespace

TEST_CASE("comm: version serial number accepts frame index 0-3", "[comm][l6][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, version_handler(kLeftId, {0, 1, 2, 3}));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto info = hand.version.get_device_info(kTimeout);
    REQUIRE(info.serial_number == std::string("LHL6-03-001-L-Z-1-A"));
    REQUIRE(info.firmware_version.major == 2);
    REQUIRE(info.pcb_version.major == 3);
    REQUIRE(info.mechanical_version.major == 3);
}

TEST_CASE("comm: version serial number accepts byte offsets 0/6/12", "[comm][l6][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, version_handler(kLeftId, {0, 6, 12}));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto info = hand.version.get_device_info(kTimeout);
    REQUIRE(info.serial_number == "LHL6 -03-105-L-B-1");
}

TEST_CASE("comm: L6 serial accepts an unpadded final indexed fragment", "[comm][l6][version]") {
    constexpr uint32_t right_id = 0x27;
    for (uint8_t dlc = 3; dlc < 8; ++dlc) {
        DYNAMIC_SECTION("final DLC " << unsigned(dlc)) {
            auto transport = std::make_unique<FakeTransport>();
            const auto versions = version_handler(right_id, {0, 1, 2, 3});
            transport->on_request(right_id, [versions, dlc, right_id](const Message &request) {
                if (request.data[0] != kSnCmd) {
                    return versions(request);
                }
                auto tail = sn_frame(right_id, 3, "1-DXYZ");
                tail.dlc = dlc;
                return std::vector<Message>{sn_frame(right_id, 0, "LHL6-0"), sn_frame(right_id, 1, "3.3-61"),
                                            sn_frame(right_id, 2, "5-R-B-"), tail};
            });
            L6 hand(Side::kRight, std::move(transport), "can0", test_util::unique_mapping_path());
            const auto info = hand.version.get_device_info(kTimeout);
            REQUIRE(info.serial_number == "LHL6-03.3-615-R-B-" + std::string("1-DXYZ", dlc - 2));
        }
    }
}

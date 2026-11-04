#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l20/l20.hpp>

#include "hand/l20/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l20::L20;
using linkerbot::hand::l20::Side;

namespace {

    constexpr uint8_t kSnCmd = 0xC0;
    constexpr uint8_t kPcbCmd = 0xC1;
    constexpr uint8_t kFwCmd = 0xC2;
    constexpr uint8_t kMechCmd = 0xC4;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    struct SnFrameArguments {
        uint32_t id;
        uint8_t frame_id;
        const char *six;
    };
    Message sn_frame(const SnFrameArguments &arguments) {
        Message msg;
        msg.id = arguments.id;
        msg.dlc = 8;
        msg.data[0] = kSnCmd;
        msg.data[1] = arguments.frame_id;
        for (uint8_t i = 0; i < 6; ++i) {
            msg.data[2 + i] = static_cast<uint8_t>(arguments.six[i]);
        }
        return msg;
    }

    struct VersionReplyArguments {
        uint32_t id;
        uint8_t cmd;
        uint8_t a;
        uint8_t b;
        uint8_t c;
    };
    Message version_reply(const VersionReplyArguments &arguments) {
        Message reply;
        reply.id = arguments.id;
        reply.dlc = 4;
        reply.data[0] = arguments.cmd;
        reply.data[1] = arguments.a;
        reply.data[2] = arguments.b;
        reply.data[3] = arguments.c;
        return reply;
    }

    FakeTransport::Handler version_handler(uint32_t id) {
        return [id](const Message &req) {
            if (req.dlc < 1) {
                return std::vector<Message>{};
            }
            switch (req.data[0]) {
            case kSnCmd:
                // BYTE1 是帧序号 0/1/2/3，不是 L20Lite 的字节偏移 0/6/12/18。
                return std::vector<Message>{sn_frame({id, 0, "LHT25-"}), sn_frame({id, 1, "SN-01-"}),
                                            sn_frame({id, 2, "L-B-2-"}), sn_frame({id, 3, "XYZ\0\0\0"})};
            case kPcbCmd:
                return std::vector<Message>{version_reply({id, kPcbCmd, 1, 2, 3})};
            case kFwCmd:
                return std::vector<Message>{version_reply({id, kFwCmd, 1, 1, 2})};
            case kMechCmd:
                return std::vector<Message>{version_reply({id, kMechCmd, 1, 0, 0})};
            default:
                return std::vector<Message>{};
            }
        };
    }

} // namespace

// 0xC0 BYTE1 是帧序号 0/1/2/3（每帧 6 字节）；0xC1/C2/C4 应答 DLC=4。

// 四帧按 frame_id 拼成 LHT25-SN-01-L-B-2-XYZ；并读出固件 1.1.2、PCB 1.2.3、机械 1。
TEST_CASE("comm: l20 version serial number uses frame_id 0/1/2/3", "[comm][l20][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, version_handler(l20_comm::kLeftId));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto info = hand.version.get_device_info(kTimeout);
    REQUIRE(info.serial_number == std::string("LHT25-SN-01-L-B-2-XYZ"));
    REQUIRE(info.firmware_version.major == 1);
    REQUIRE(info.firmware_version.minor == 1);
    REQUIRE(info.firmware_version.patch == 2);
    REQUIRE(info.pcb_version.major == 1);
    REQUIRE(info.pcb_version.minor == 2);
    REQUIRE(info.pcb_version.patch == 3);
    REQUIRE(info.mechanical_version.major == 1);
    REQUIRE(info.mechanical_version.minor == 0);
    REQUIRE(info.mechanical_version.patch == 0);
}

// 查 PCB/固件/机械只发单字节 0xC1/0xC2/0xC4。
TEST_CASE("comm: l20 version queries are single-byte 0xC1/C2/C4", "[comm][l20][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, version_handler(l20_comm::kLeftId));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    (void)hand.version.get_device_info(kTimeout);

    bool saw_pcb = false;
    bool saw_fw = false;
    bool saw_mech = false;
    for (const auto &msg : raw->sent()) {
        if (msg.dlc < 1) {
            continue;
        }
        if (msg.data[0] == kPcbCmd) {
            saw_pcb = true;
            REQUIRE(msg.dlc == 1);
        }
        if (msg.data[0] == kFwCmd) {
            saw_fw = true;
            REQUIRE(msg.dlc == 1);
        }
        if (msg.data[0] == kMechCmd) {
            saw_mech = true;
            REQUIRE(msg.dlc == 1);
        }
    }
    REQUIRE(saw_pcb);
    REQUIRE(saw_fw);
    REQUIRE(saw_mech);
}

TEST_CASE("comm: L20 serial accepts an unpadded final indexed fragment", "[comm][l20][version]") {
    for (uint8_t dlc = 3; dlc < 8; ++dlc) {
        DYNAMIC_SECTION("final DLC " << unsigned(dlc)) {
            auto transport = std::make_unique<FakeTransport>();
            const auto versions = version_handler(l20_comm::kRightId);
            transport->on_request(l20_comm::kRightId, [versions, dlc](const Message &request) {
                if (request.data[0] != kSnCmd) {
                    return versions(request);
                }
                auto tail = sn_frame({l20_comm::kRightId, 3, "1-CXYZ"});
                tail.dlc = dlc;
                // Bytes beyond DLC intentionally remain nonzero.
                return std::vector<Message>{sn_frame({l20_comm::kRightId, 0, "LHT20-"}),
                                            sn_frame({l20_comm::kRightId, 1, "010-11"}),
                                            sn_frame({l20_comm::kRightId, 2, "2-R-B-"}), tail};
            });
            L20 hand(Side::kRight, std::move(transport), "can0", test_util::unique_mapping_path());
            const auto info = hand.version.get_device_info(kTimeout);
            REQUIRE(info.serial_number == "LHT20-010-112-R-B-" + std::string("1-CXYZ", dlc - 2));
        }
    }
}

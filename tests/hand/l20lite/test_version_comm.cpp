#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::Side;

namespace {

    constexpr uint8_t kSnCmd = 0xC0;
    constexpr uint8_t kPcbCmd = 0xC1;
    constexpr uint8_t kFwCmd = 0xC2;
    constexpr uint8_t kMechCmd = 0xC4;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    Message sn_frame(uint32_t id, uint8_t offset, const char *six) {
        Message msg;
        msg.id = id;
        msg.dlc = 8;
        msg.data[0] = kSnCmd;
        msg.data[1] = offset;
        for (uint8_t i = 0; i < 6; ++i) {
            msg.data[2 + i] = static_cast<uint8_t>(six[i]);
        }
        return msg;
    }

    Message version_reply(uint32_t id, uint8_t cmd, uint8_t a, uint8_t b, uint8_t c) {
        Message reply;
        reply.id = id;
        reply.dlc = 4;
        reply.data[0] = cmd;
        reply.data[1] = a;
        reply.data[2] = b;
        reply.data[3] = c;
        return reply;
    }

    FakeTransport::Handler version_handler(uint32_t id) {
        return [id](const Message &req) {
            if (req.dlc < 1) {
                return std::vector<Message>{};
            }
            switch (req.data[0]) {
            case kSnCmd:
                return std::vector<Message>{sn_frame(id, 0, "LHT10-"), sn_frame(id, 6, "03-12-"),
                                            sn_frame(id, 12, "L-B-1-"), sn_frame(id, 18, "A\0\0\0\0\0")};
            case kPcbCmd:
                return std::vector<Message>{version_reply(id, kPcbCmd, 1, 2, 3)};
            case kFwCmd:
                return std::vector<Message>{version_reply(id, kFwCmd, 1, 1, 2)};
            case kMechCmd:
                return std::vector<Message>{version_reply(id, kMechCmd, 1, 0, 0)};
            default:
                return std::vector<Message>{};
            }
        };
    }

} // namespace

// 新协议 0xC0 BYTE1 是字节偏移 0/6/12/18；0xC1 只发一字节。

// 串号按偏移 0/6/12 拼成 LHT10-03-12-L-B-1-；并读出固件 1.1.2、PCB 1.2.3、机械 1。
TEST_CASE("comm: version serial number uses byte offsets 0/6/12/18", "[comm][l20lite][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, version_handler(l20lite_comm::kLeftId));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto info = hand.version.get_device_info(kTimeout);
    // 新协议「10 一般三帧」：0/6/12 到齐即组装，第 4 帧（偏移 18）可选。
    REQUIRE(info.serial_number == std::string("LHT10-03-12-L-B-1-"));
    REQUIRE(info.firmware_version.major == 1);
    REQUIRE(info.firmware_version.minor == 1);
    REQUIRE(info.firmware_version.patch == 2);
    REQUIRE(info.pcb_version.major == 1);
    REQUIRE(info.pcb_version.minor == 2);
    REQUIRE(info.pcb_version.patch == 3);
    REQUIRE(info.mechanical_version.major == 1);
}

// 只有偏移 0/6/12 三帧也应拼出完整串号 ABC123DEF456GHI。
TEST_CASE("comm: version serial number completes on three frames", "[comm][l20lite][version]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId, [](const Message &req) {
        if (req.dlc < 1) {
            return std::vector<Message>{};
        }
        if (req.data[0] == kSnCmd) {
            return std::vector<Message>{sn_frame(l20lite_comm::kLeftId, 0, "ABC123"),
                                        sn_frame(l20lite_comm::kLeftId, 6, "DEF456"),
                                        sn_frame(l20lite_comm::kLeftId, 12, "GHI\0\0\0")};
        }
        if (req.data[0] == kPcbCmd) {
            return std::vector<Message>{version_reply(l20lite_comm::kLeftId, kPcbCmd, 1, 0, 0)};
        }
        if (req.data[0] == kFwCmd) {
            return std::vector<Message>{version_reply(l20lite_comm::kLeftId, kFwCmd, 1, 0, 0)};
        }
        if (req.data[0] == kMechCmd) {
            return std::vector<Message>{version_reply(l20lite_comm::kLeftId, kMechCmd, 1, 0, 0)};
        }
        return std::vector<Message>{};
    });

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    REQUIRE(hand.version.get_device_info(kTimeout).serial_number == std::string("ABC123DEF456GHI"));
}

// 查 PCB 只发单字节 0xC1。
TEST_CASE("comm: version PCB query is a single-byte 0xC1", "[comm][l20lite][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, version_handler(l20lite_comm::kLeftId));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    (void)hand.version.get_device_info(kTimeout);

    bool saw_pcb = false;
    for (const auto &msg : raw->sent()) {
        if (msg.dlc >= 1 && msg.data[0] == kPcbCmd) {
            saw_pcb = true;
            REQUIRE(msg.dlc == 1);
        }
    }
    REQUIRE(saw_pcb);
}

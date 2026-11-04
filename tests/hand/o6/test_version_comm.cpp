#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/o6/o6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::o6::O6;
using linkerbot::hand::o6::Side;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kSnCmd = 0xC0;
    constexpr uint8_t kPcbCmd = 0xC1;
    constexpr uint8_t kFwCmd = 0xC2;
    constexpr uint8_t kMechCmd = 0xC4;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    std::vector<uint8_t> last_payload(FakeTransport *transport) {
        const auto sent = transport->sent();
        REQUIRE_FALSE(sent.empty());
        const auto &msg = sent.back();
        return {msg.data.begin(), msg.data.begin() + msg.dlc};
    }

    struct SnFrameArguments {
        uint32_t id;
        uint8_t key;
        const char *six;
    };
    Message sn_frame(const SnFrameArguments &arguments) {
        Message msg;
        msg.id = arguments.id;
        msg.dlc = 8;
        msg.data[0] = kSnCmd;
        msg.data[1] = arguments.key;
        for (uint8_t i = 0; i < 6; ++i) {
            msg.data[2 + i] = static_cast<uint8_t>(arguments.six[i]);
        }
        return msg;
    }

    FakeTransport::Handler version_handler(uint32_t id) {
        return [id](const Message &req) {
            if (req.dlc < 1) {
                return std::vector<Message>{};
            }
            switch (req.data[0]) {
            case kSnCmd:
                return std::vector<Message>{sn_frame({id, 0, "LHO6-0"}), sn_frame({id, 1, "3-123-"}),
                                            sn_frame({id, 2, "L-B-1-"}), sn_frame({id, 3, "A\0\0\0\0\0"})};
            case kPcbCmd: {
                Message reply;
                reply.id = id;
                reply.dlc = 4;
                reply.data[0] = kPcbCmd;
                reply.data[1] = 1;
                reply.data[2] = 2;
                reply.data[3] = 3;
                return std::vector<Message>{reply};
            }
            case kFwCmd: {
                Message reply;
                reply.id = id;
                reply.dlc = 4;
                reply.data[0] = kFwCmd;
                reply.data[1] = 1;
                reply.data[2] = 1;
                reply.data[3] = 2;
                return std::vector<Message>{reply};
            }
            case kMechCmd: {
                Message reply;
                reply.id = id;
                reply.dlc = 4;
                reply.data[0] = kMechCmd;
                reply.data[1] = 1;
                reply.data[2] = 0;
                reply.data[3] = 0;
                return std::vector<Message>{reply};
            }
            default:
                return std::vector<Message>{};
            }
        };
    }

} // namespace

// 新协议 0xC0 BYTE1 是行索引 0–3；0xC1 只发一字节，回 [C1, maj, min, pat]。

TEST_CASE("comm: version serial number uses frame index 0-3", "[comm][o6][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, version_handler(kLeftId));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto info = hand.version.get_device_info(kTimeout);
    REQUIRE(info.serial_number == std::string("LHO6-03-123-L-B-1-A"));
    REQUIRE(info.firmware_version.major == 1);
    REQUIRE(info.firmware_version.minor == 1);
    REQUIRE(info.firmware_version.patch == 2);
    REQUIRE(info.pcb_version.major == 1);
    REQUIRE(info.pcb_version.minor == 2);
    REQUIRE(info.pcb_version.patch == 3);
    REQUIRE(info.mechanical_version.major == 1);
}

TEST_CASE("comm: version PCB query is a single-byte 0xC1", "[comm][o6][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, version_handler(kLeftId));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    (void)hand.version.get_device_info(kTimeout);

    bool saw_pcb = false;
    for (const auto &msg : raw->sent()) {
        if (msg.dlc >= 1 && msg.data[0] == kPcbCmd) {
            saw_pcb = true;
            REQUIRE(msg.dlc == 1);
        }
    }
    REQUIRE(saw_pcb);
    (void)last_payload(raw);
}

TEST_CASE("comm: O6 serial accepts an unpadded final indexed fragment", "[comm][o6][version]") {
    for (uint8_t dlc = 3; dlc < 8; ++dlc) {
        DYNAMIC_SECTION("final DLC " << unsigned(dlc)) {
            auto transport = std::make_unique<FakeTransport>();
            const auto versions = version_handler(kLeftId);
            transport->on_request(kLeftId, [versions, dlc](const Message &request) {
                if (request.data[0] != kSnCmd) {
                    return versions(request);
                }
                auto tail = sn_frame({kLeftId, 3, "EABCDE"});
                tail.dlc = dlc;
                // Bytes beyond DLC remain nonzero and must not be read.
                return std::vector<Message>{sn_frame({kLeftId, 0, "LHO6-0"}), sn_frame({kLeftId, 1, "4-021-"}),
                                            sn_frame({kLeftId, 2, "L-Z-1-"}), tail};
            });
            O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
            const auto info = hand.version.get_device_info(kTimeout);
            REQUIRE(info.serial_number == "LHO6-04-021-L-Z-1-" + std::string("EABCDE", dlc - 2));
        }
    }
}

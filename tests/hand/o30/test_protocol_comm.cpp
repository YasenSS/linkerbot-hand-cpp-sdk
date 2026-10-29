#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/protocol.hpp>
#include <linkerbot/hand/o30/transaction.hpp>

using linkerbot::can::kMaxDataLen;
using linkerbot::hand::o30::kJointCount;
using linkerbot::hand::o30::kSlotCount;
using linkerbot::hand::o30::project_u16;
using linkerbot::hand::o30::protocol::as_u16_le_slots;
using linkerbot::hand::o30::protocol::as_u8_slots;
using linkerbot::hand::o30::protocol::build_message;
using linkerbot::hand::o30::protocol::canfd_wire_bytes;
using linkerbot::hand::o30::protocol::ctrl_byte;
using linkerbot::hand::o30::protocol::decode_ascii;
using linkerbot::hand::o30::protocol::error_code_of;
using linkerbot::hand::o30::protocol::error_message;
using linkerbot::hand::o30::protocol::expected_response_id;
using linkerbot::hand::o30::protocol::HopFrame;
using linkerbot::hand::o30::protocol::is_error_response;
using linkerbot::hand::o30::protocol::kDefaultRequestId;
using linkerbot::hand::o30::protocol::kErrorIllegalValue;
using linkerbot::hand::o30::protocol::kErrorSubIndexMissing;
using linkerbot::hand::o30::protocol::kHeaderBytes;
using linkerbot::hand::o30::protocol::kMaxHopPayload;
using linkerbot::hand::o30::protocol::kMiCommError;
using linkerbot::hand::o30::protocol::kMiPosition;
using linkerbot::hand::o30::protocol::kProductInfoBytes;
using linkerbot::hand::o30::protocol::kResponseIdOffset;
using linkerbot::hand::o30::protocol::kU16ObjectBytes;
using linkerbot::hand::o30::protocol::mi_of;
using linkerbot::hand::o30::protocol::parse_frame;
using linkerbot::hand::o30::protocol::ProtocolError;
using linkerbot::hand::o30::protocol::read_u16_le;
using linkerbot::hand::o30::protocol::require_positive_timeout;
using linkerbot::hand::o30::protocol::rts_of;

namespace {
    template <typename Fn> std::string protocol_error_message(Fn action) {
        try {
            action();
        } catch (const ProtocolError &error) {
            return error.what();
        }
        return {};
    }
} // namespace

// HOP 帧编解码：标准 CAN ID、EDL 截断、CAN FD 合法长度、错误帧与 36/72 字节对象。

// 默认请求 0x001，应答 = 请求 + 0x400；越界 11-bit 由 Client 拒绝。
TEST_CASE("comm: default CAN IDs are standard 0x001 / 0x401", "[comm][o30][protocol]") {
    REQUIRE(kDefaultRequestId == 0x001U);
    REQUIRE(kResponseIdOffset == 0x400U);
    REQUIRE(expected_response_id(0x001) == 0x401U);
    REQUIRE(expected_response_id(0x123) == 0x523U);
}

// CTRL = (RTS<<7)|MI；MI 必须落在 7 bit。
TEST_CASE("comm: CTRL packs MI and RTS", "[comm][o30][protocol]") {
    REQUIRE(ctrl_byte(0x01, false) == 0x01);
    REQUIRE(ctrl_byte(0x01, true) == 0x81);
    REQUIRE(mi_of(0x81) == 0x01);
    REQUIRE(rts_of(0x81));
    REQUIRE_FALSE(rts_of(0x01));
    REQUIRE_THROWS_AS(ctrl_byte(0x80, false), linkerbot::hand::ValidationError);
}

// 读请求只有 3 字节头，DLC 就是 3；写请求补到合法 CAN FD 长度。
TEST_CASE("comm: build_message uses standard FD frames without BRS", "[comm][o30][protocol]") {
    const auto read = build_message(0x001, kMiPosition, 0, 36, {}, false);
    REQUIRE(read.id == 0x001);
    REQUIRE_FALSE(read.is_extended);
    REQUIRE(read.is_fd);
    REQUIRE_FALSE(read.is_brs);
    REQUIRE(read.dlc == 3);
    REQUIRE(read.data[0] == 0x01);
    REQUIRE(read.data[1] == 0x00);
    REQUIRE(read.data[2] == 36);

    const auto write = build_message(0x001, kMiPosition, 0x05, 10, std::vector<uint8_t>(10, 0x80), false);
    REQUIRE(write.dlc == 16);
    REQUIRE(write.data[0] == 0x01);
    REQUIRE(write.data[1] == 0x05);
    REQUIRE(write.data[2] == 10);
    for (size_t i = 0; i < 10; ++i) {
        REQUIRE(write.data[3 + i] == 0x80);
    }
}

// 设定值读 CTRL 带 0x80。
TEST_CASE("comm: setpoint read sets RTS on CTRL", "[comm][o30][protocol]") {
    const auto msg = build_message(0x001, kMiPosition, 0, 36, {}, true);
    REQUIRE(msg.data[0] == 0x81);
}

// CAN FD 长度跳到 0-8 / 12 / 16 / 20 / 24 / 32 / 48 / 64。
TEST_CASE("comm: canfd_wire_bytes snaps to legal DLC", "[comm][o30][protocol]") {
    REQUIRE(canfd_wire_bytes(3) == 3);
    REQUIRE(canfd_wire_bytes(9) == 12);
    REQUIRE(canfd_wire_bytes(13) == 16);
    REQUIRE(canfd_wire_bytes(48) == 48);
    REQUIRE(canfd_wire_bytes(61) == 64);
    REQUIRE(canfd_wire_bytes(64) == 64);
    REQUIRE_THROWS_AS(canfd_wire_bytes(kMaxDataLen + 1), linkerbot::hand::ValidationError);
}

// parse_frame 按 EDL 截断，忽略 CAN FD 填充。
TEST_CASE("comm: parse_frame truncates to EDL and ignores padding", "[comm][o30][protocol]") {
    std::vector<uint8_t> bytes{0x01, 0x00, 0x03, 0xAA, 0xBB, 0xCC};
    bytes.resize(16, 0x00);
    const auto frame = parse_frame(bytes.data(), bytes.size());
    REQUIRE(frame.mi == kMiPosition);
    REQUIRE_FALSE(frame.rts);
    REQUIRE(frame.si == 0);
    REQUIRE(frame.edl == 3);
    REQUIRE(frame.payload == std::vector<uint8_t>({0xAA, 0xBB, 0xCC}));
}

// 头不足或 payload 短于 EDL 抛 ProtocolError。
TEST_CASE("comm: parse_frame rejects truncated HOP frames", "[comm][o30][protocol]") {
    const uint8_t short_header[] = {0x01, 0x00};
    REQUIRE_THROWS_AS(parse_frame(short_header, 2), ProtocolError);

    const uint8_t short_payload[] = {0x01, 0x00, 0x04, 0x01, 0x02};
    const auto message =
        protocol_error_message([&] { static_cast<void>(parse_frame(short_payload, sizeof(short_payload))); });
    REQUIRE(message.find("EDL") != std::string::npos);
}

// 非 0x4F 请求收到 4F / EDL=1 才算错误应答；主动读 0x4F 不算失败。
TEST_CASE("comm: 4F is an error only for a different request MI", "[comm][o30][protocol]") {
    HopFrame error{};
    error.mi = kMiCommError;
    error.edl = 1;
    error.payload = {kErrorSubIndexMissing};
    REQUIRE(is_error_response(error, kMiPosition));
    REQUIRE_FALSE(is_error_response(error, kMiCommError));
    REQUIRE(error_code_of(error) == kErrorSubIndexMissing);
    REQUIRE(error_message(kErrorSubIndexMissing).find("sub-index") != std::string::npos);
    REQUIRE(error_message(kErrorIllegalValue).find("illegal") != std::string::npos);
}

// 36 字节对象长度不对就抛；半字对象必须 72 字节小端。
TEST_CASE("comm: u8 and u16 slot decoders enforce object size", "[comm][o30][protocol]") {
    REQUIRE_THROWS_AS(as_u8_slots(std::vector<uint8_t>(35, 0)), ProtocolError);
    REQUIRE_THROWS_AS(as_u16_le_slots(std::vector<uint8_t>(71, 0)), ProtocolError);

    std::vector<uint8_t> u8(36, 0);
    for (size_t i = 0; i < u8.size(); ++i) {
        u8[i] = static_cast<uint8_t>(i);
    }
    REQUIRE(as_u8_slots(u8)[24] == 24);

    std::vector<uint8_t> u16(kU16ObjectBytes, 0);
    u16[0] = 0xFE;
    u16[1] = 0xFF;
    u16[10] = 0x02;
    u16[11] = 0x00;
    const auto slots = as_u16_le_slots(u16);
    REQUIRE(slots[0] == 0xFFFE);
    REQUIRE(slots[5] == 0x0002);
    const auto joints = project_u16(slots);
    REQUIRE(joints.size() == kJointCount);
    REQUIRE(joints[0] == 0xFFFE);
    REQUIRE(joints[1] == 0x0002);
}

// 小端 u16 与 ASCII 去尾零/空格。
TEST_CASE("comm: little-endian helpers and ASCII trim", "[comm][o30][protocol]") {
    const uint8_t bytes[] = {0x34, 0x12, 0x00, 0x00};
    REQUIRE(read_u16_le(bytes) == 0x1234);
    REQUIRE(decode_ascii({'H', 'O', 'P', 0, ' '}) == "HOP");
    REQUIRE(kProductInfoBytes == 225);
    REQUIRE(kMaxHopPayload == 61);
    REQUIRE(kHeaderBytes == 3);
    REQUIRE(kSlotCount == 36);
}

// timeout_ms 必须为正。
TEST_CASE("comm: require_positive_timeout rejects zero and negative", "[comm][o30][protocol]") {
    REQUIRE_NOTHROW(require_positive_timeout(std::chrono::milliseconds{1}));
    REQUIRE_THROWS_AS(require_positive_timeout(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(require_positive_timeout(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
}

// 单帧写超过 61 字节 payload 应拒。
TEST_CASE("comm: build_message rejects a payload larger than one HOP fragment", "[comm][o30][protocol]") {
    REQUIRE_THROWS_AS(build_message(0x001, kMiPosition, 0, 62, std::vector<uint8_t>(62, 1), false),
                      linkerbot::hand::ValidationError);
}

TEST_CASE("comm: O30 writes expose semantic command families", "[comm][o30][protocol][health]") {
    using namespace linkerbot::hand::o30::protocol;
    const auto family = [](uint8_t mi) { return transaction(kDefaultRequestId, mi, 0, 1, {1}, false).command_family; };
    REQUIRE(family(kMiPosition) == "angle_targets");
    REQUIRE(family(kMiSpeed) == "speed_targets");
    REQUIRE(family(kMiCurrent) == "current_limits");
    REQUIRE(family(kMiAcceleration) == "acceleration_targets");
    REQUIRE(family(kMiStallTime) == "stall");
    REQUIRE(family(kMiConfig) == "configuration");
    REQUIRE(family(kMiVoltage) == "hop_writes");
}

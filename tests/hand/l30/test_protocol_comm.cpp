#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/protocol.hpp>
#include <linkerbot/hand/l30/protocol_backend.hpp>

using linkerbot::can::kMaxDataLen;
using linkerbot::hand::l30::kJointCount;
using linkerbot::hand::l30::protocol::ack_payload;
using linkerbot::hand::l30::protocol::assemble_tactile_payload;
using linkerbot::hand::l30::protocol::build_can_id;
using linkerbot::hand::l30::protocol::build_message;
using linkerbot::hand::l30::protocol::check_response_status;
using linkerbot::hand::l30::protocol::decode_ascii_response;
using linkerbot::hand::l30::protocol::decode_i16_response;
using linkerbot::hand::l30::protocol::decode_periodic_i16_report;
using linkerbot::hand::l30::protocol::decode_periodic_u8_report;
using linkerbot::hand::l30::protocol::decode_response_payload;
using linkerbot::hand::l30::protocol::decode_u8_response;
using linkerbot::hand::l30::protocol::empty_request_payload;
using linkerbot::hand::l30::protocol::encode_i16_vector;
using linkerbot::hand::l30::protocol::encode_periodic_config;
using linkerbot::hand::l30::protocol::encode_u16_vector;
using linkerbot::hand::l30::protocol::expected_response_id;
using linkerbot::hand::l30::protocol::FrameId;
using linkerbot::hand::l30::protocol::kAccessRead;
using linkerbot::hand::l30::protocol::kAccessWrite;
using linkerbot::hand::l30::protocol::kJointMaskLimit;
using linkerbot::hand::l30::protocol::kParentConfig;
using linkerbot::hand::l30::protocol::kParentControl;
using linkerbot::hand::l30::protocol::kParentPeriodic;
using linkerbot::hand::l30::protocol::kParentQuery;
using linkerbot::hand::l30::protocol::kParentTactile;
using linkerbot::hand::l30::protocol::kSingleFrameTransaction;
using linkerbot::hand::l30::protocol::kStatusOk;
using linkerbot::hand::l30::protocol::kSubConfigUnlock;
using linkerbot::hand::l30::protocol::kSubCurrent;
using linkerbot::hand::l30::protocol::kSubCurrentLimit;
using linkerbot::hand::l30::protocol::kSubDeviceInfo;
using linkerbot::hand::l30::protocol::kSubDisable;
using linkerbot::hand::l30::protocol::kSubEnable;
using linkerbot::hand::l30::protocol::kSubFault;
using linkerbot::hand::l30::protocol::kSubPosition;
using linkerbot::hand::l30::protocol::kSubSpeed;
using linkerbot::hand::l30::protocol::kSubTactileThumb;
using linkerbot::hand::l30::protocol::kSubTemperature;
using linkerbot::hand::l30::protocol::kTactileDataLength;
using linkerbot::hand::l30::protocol::kTactileFirstLength;
using linkerbot::hand::l30::protocol::kTactileSecondLength;
using linkerbot::hand::l30::protocol::kU8VectorPayloadBytes;
using linkerbot::hand::l30::protocol::kVectorPayloadBytes;
using linkerbot::hand::l30::protocol::kVectorWireBytes;
using linkerbot::hand::l30::protocol::parse_can_id;
using linkerbot::hand::l30::protocol::ProtocolError;
using linkerbot::hand::l30::protocol::reported_joint_count;
using linkerbot::hand::l30::protocol::status_message;

namespace {
    constexpr uint8_t kNodeId = 1;
    constexpr uint8_t kHostId = 0;

    // 一条真机命令的仲裁 ID 期望值，request/response 都由实机报文核对过。
    struct IdCase {
        uint8_t access;
        uint8_t parent;
        uint8_t subcmd;
        uint32_t request;
        uint32_t response;
    };

    FrameId request_fields(const IdCase &item) {
        FrameId fields{};
        fields.access = item.access;
        fields.parent = item.parent;
        fields.subcmd = item.subcmd;
        fields.dst_id = kNodeId;
        fields.src_id = kHostId;
        return fields;
    }

    std::vector<IdCase> golden_ids() {
        return {
            {kAccessWrite, kParentControl, kSubEnable, 0x0220E100U, 0x0220E008U},
            {kAccessWrite, kParentControl, kSubDisable, 0x02210100U, 0x02210008U},
            {kAccessWrite, kParentControl, kSubPosition, 0x02202100U, 0x02202008U},
            {kAccessWrite, kParentControl, kSubCurrentLimit, 0x02204100U, 0x02204008U},
            {kAccessWrite, kParentControl, kSubSpeed, 0x02206100U, 0x02206008U},
            {kAccessRead, kParentQuery, kSubPosition, 0x00A02100U, 0x00A02008U},
            {kAccessRead, kParentQuery, kSubCurrent, 0x00A04100U, 0x00A04008U},
            {kAccessRead, kParentQuery, kSubSpeed, 0x00A06100U, 0x00A06008U},
            {kAccessRead, kParentQuery, kSubTemperature, 0x00A08100U, 0x00A08008U},
            {kAccessRead, kParentQuery, kSubFault, 0x00A0A100U, 0x00A0A008U},
            {kAccessWrite, kParentPeriodic, kSubPosition, 0x02802100U, 0x02802008U},
            {kAccessWrite, kParentConfig, kSubConfigUnlock, 0x02602100U, 0x02602008U},
            {kAccessRead, kParentConfig, kSubDeviceInfo, 0x00604100U, 0x00604008U},
            {kAccessRead, kParentTactile, kSubTactileThumb, 0x00402100U, 0x00402008U},
        };
    }

    std::vector<uint8_t> i16_frame(uint8_t declared, uint8_t transaction, uint8_t status, size_t data_bytes) {
        std::vector<uint8_t> frame{declared, transaction, status};
        for (size_t i = 0; i < data_bytes; ++i) {
            frame.push_back(static_cast<uint8_t>(i));
        }
        return frame;
    }

    // 触觉分帧：BYTE0 长度、BYTE1 事务码、BYTE2 状态，之后是 offset 起的斜坡数据。
    std::vector<uint8_t> tactile_frame(uint8_t transaction, size_t length, size_t offset, size_t padding,
                                       uint8_t status = kStatusOk) {
        std::vector<uint8_t> frame{static_cast<uint8_t>(length), transaction, status};
        for (size_t i = 0; i < length; ++i) {
            frame.push_back(static_cast<uint8_t>(offset + i));
        }
        frame.resize(frame.size() + padding, 0x00);
        return frame;
    }

    // 仓库没有 matcher，异常文案只能手动捕获后用 find 断言。
    template <typename Fn> std::string protocol_error_message(Fn action) {
        try {
            action();
        } catch (const ProtocolError &error) {
            return error.what();
        }
        return {};
    }

    // 未抛异常返回 -1，避免和真实状态码 0x00 混淆。
    template <typename Fn> int protocol_error_status(Fn action) {
        try {
            action();
        } catch (const ProtocolError &error) {
            return static_cast<int>(error.status());
        }
        return -1;
    }

    template <typename Fn> std::string invalid_argument_message(Fn action) {
        try {
            action();
        } catch (const linkerbot::hand::ValidationError &error) {
            return error.what();
        }
        return {};
    }
} // namespace

// L30 protocol 纯单元测试：仲裁 ID 位域编解码、请求负载编码、应答/周期上报解码、
// 触觉双帧拼接与状态码文案，全部直接调函数，不经 FakeTransport 也不建 L30 对象。

// 14 条真机命令的 request/response 仲裁 ID 与 to_int() 逐一对齐黄金值。
TEST_CASE("comm: build_can_id matches the golden arbitration IDs", "[comm][l30][protocol]") {
    for (const auto &item : golden_ids()) {
        const auto fields = request_fields(item);
        REQUIRE(build_can_id(fields) == item.request);
        REQUIRE(fields.to_int() == item.request);
        REQUIRE(build_can_id(fields.response()) == item.response);
    }
}

// parse_can_id 还原六个字段；response() 交换 Dst/Src 但保留 Access，写 ACK 仍是 Access=1。
TEST_CASE("comm: parse_can_id round-trips every field and response keeps access", "[comm][l30][protocol]") {
    const auto fields = parse_can_id(0x0220E100U);
    REQUIRE(fields.priority == 0);
    REQUIRE(fields.access == 1);
    REQUIRE(fields.parent == 1);
    REQUIRE(fields.subcmd == 7);
    REQUIRE(fields.dst_id == 1);
    REQUIRE(fields.src_id == 0);

    const auto reply = fields.response();
    REQUIRE(reply.access == kAccessWrite);
    REQUIRE(reply.priority == fields.priority);
    REQUIRE(reply.parent == fields.parent);
    REQUIRE(reply.subcmd == fields.subcmd);
    REQUIRE(reply.dst_id == 0);
    REQUIRE(reply.src_id == 1);
    REQUIRE(reply.to_int() == 0x0220E008U);

    for (const auto &item : golden_ids()) {
        const auto parsed = parse_can_id(item.request);
        REQUIRE(parsed.access == item.access);
        REQUIRE(parsed.parent == item.parent);
        REQUIRE(parsed.subcmd == item.subcmd);
        REQUIRE(parsed.dst_id == kNodeId);
        REQUIRE(parsed.src_id == kHostId);
        REQUIRE(parsed.to_int() == item.request);
    }
}

// 超过 29 位或占用保留低 3 位的仲裁 ID 都要拒。
TEST_CASE("comm: parse_can_id rejects oversized and reserved-bit IDs", "[comm][l30][protocol]") {
    REQUIRE_THROWS_AS(parse_can_id(0x20000000U), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(parse_can_id(0xFFFFFFF8U), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(parse_can_id(0x0220E101U), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(parse_can_id(0x00000007U), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(parse_can_id(0x1FFFFFF8U));

    const auto wide = invalid_argument_message([] { (void)parse_can_id(0x20000000U); });
    REQUIRE(wide.find("29 bits") != std::string::npos);
    const auto reserved = invalid_argument_message([] { (void)parse_can_id(0x0220E101U); });
    REQUIRE(reserved.find("reserved") != std::string::npos);
}

// build_can_id 的字段宽度校验：priority>7、access>1、parent>0xF、节点号>31 全抛。
TEST_CASE("comm: build_can_id rejects out-of-range fields", "[comm][l30][protocol]") {
    FrameId fields{};
    fields.parent = kParentControl;
    fields.subcmd = kSubPosition;
    fields.dst_id = kNodeId;
    REQUIRE_NOTHROW(build_can_id(fields));

    auto bad = fields;
    bad.priority = 8;
    REQUIRE_THROWS_AS(build_can_id(bad), linkerbot::hand::ValidationError);

    bad = fields;
    bad.access = 2;
    REQUIRE_THROWS_AS(build_can_id(bad), linkerbot::hand::ValidationError);

    bad = fields;
    bad.parent = 0x10;
    REQUIRE_THROWS_AS(build_can_id(bad), linkerbot::hand::ValidationError);

    bad = fields;
    bad.dst_id = 32;
    REQUIRE_THROWS_AS(build_can_id(bad), linkerbot::hand::ValidationError);

    bad = fields;
    bad.src_id = 32;
    REQUIRE_THROWS_AS(build_can_id(bad), linkerbot::hand::ValidationError);

    bad = fields;
    bad.dst_id = 31;
    bad.src_id = 31;
    REQUIRE_NOTHROW(build_can_id(bad));
}

// expected_response_id 等价于 parse → response → build。
TEST_CASE("comm: expected_response_id swaps the node IDs", "[comm][l30][protocol]") {
    for (const auto &item : golden_ids()) {
        REQUIRE(expected_response_id(item.request) == item.response);
    }
    REQUIRE(expected_response_id(0x00802008U) == 0x00802100U);
}

// build_message 补零到 wire_bytes，dlc 等于 wire_bytes，并置扩展帧与 FD 标志。
TEST_CASE("comm: build_message zero-pads out to the wire length", "[comm][l30][protocol]") {
    std::array<int16_t, kJointCount> values{};
    values.fill(0);
    values[0] = 0x1234;
    const auto payload = encode_i16_vector(values);
    REQUIRE(payload.size() == 36);

    FrameId fields{};
    fields.access = kAccessWrite;
    fields.parent = kParentControl;
    fields.subcmd = kSubPosition;
    fields.dst_id = kNodeId;

    const auto msg = build_message(fields, payload, kVectorWireBytes);
    REQUIRE(msg.id == 0x02202100U);
    REQUIRE(msg.is_extended);
    REQUIRE(msg.is_fd);
    REQUIRE(msg.dlc == kVectorWireBytes);
    for (size_t i = 0; i < payload.size(); ++i) {
        REQUIRE(msg.data[i] == payload[i]);
    }
    for (size_t i = payload.size(); i < kVectorWireBytes; ++i) {
        REQUIRE(msg.data[i] == 0x00);
    }
}

// 负载超出 wire_bytes、或 wire_bytes 超过 CAN FD 上限 64，都要拒。
TEST_CASE("comm: build_message rejects impossible wire lengths", "[comm][l30][protocol]") {
    FrameId fields{};
    fields.parent = kParentControl;
    fields.subcmd = kSubPosition;
    fields.dst_id = kNodeId;

    const std::vector<uint8_t> four{0x02, 0x00, 0x11, 0x22};
    REQUIRE_THROWS_AS(build_message(fields, four, 3), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(build_message(fields, four, 4));

    const auto oversized = invalid_argument_message([&] { (void)build_message(fields, four, 3); });
    REQUIRE(oversized.find("exceeds the declared wire length") != std::string::npos);

    const auto wide = static_cast<uint8_t>(kMaxDataLen + 1);
    REQUIRE_THROWS_AS(build_message(fields, {}, wide), linkerbot::hand::ValidationError);
    const auto message = invalid_argument_message([&] { (void)build_message(fields, {}, wide); });
    REQUIRE(message.find("CAN FD maximum") != std::string::npos);
    REQUIRE_NOTHROW(build_message(fields, {}, static_cast<uint8_t>(kMaxDataLen)));
}

// 空请求是 {00, 00}；ACK 是 {00, 00, status}。
TEST_CASE("comm: empty_request_payload and ack_payload have fixed shapes", "[comm][l30][protocol]") {
    REQUIRE(empty_request_payload() == std::vector<uint8_t>{0x00, 0x00});
    REQUIRE(ack_payload() == std::vector<uint8_t>{0x00, 0x00, kStatusOk});
    REQUIRE(ack_payload(0x20) == std::vector<uint8_t>{0x00, 0x00, 0x20});
    REQUIRE(ack_payload(0xF0) == std::vector<uint8_t>{0x00, 0x00, 0xF0});
}

// i16 向量帧：头 22 00，随后 17 个大端 i16；负值按二进制补码写（-200 → FF 38）。
TEST_CASE("comm: encode_i16_vector writes big-endian two's complement", "[comm][l30][protocol]") {
    std::array<int16_t, kJointCount> values{};
    values.fill(0);
    values[0] = 0;
    values[1] = 1;
    values[2] = -1;
    values[3] = 200;
    values[4] = -200;
    values[5] = 900;
    values[6] = 1200;
    values[16] = -1000;

    const std::vector<uint8_t> expected{kVectorPayloadBytes,
                                        kSingleFrameTransaction,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x01,
                                        0xFF,
                                        0xFF,
                                        0x00,
                                        0xC8,
                                        0xFF,
                                        0x38,
                                        0x03,
                                        0x84,
                                        0x04,
                                        0xB0,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0x00,
                                        0xFC,
                                        0x18};
    const auto encoded = encode_i16_vector(values);
    REQUIRE(encoded.size() == 36);
    REQUIRE(encoded == expected);
}

// u16 向量帧共用 22 00 头，17 个值按大端逐一落位。
TEST_CASE("comm: encode_u16_vector writes big-endian unsigned words", "[comm][l30][protocol]") {
    const std::array<uint16_t, kJointCount> values{60, 800, 65535, 1,  0,  300, 12, 13, 14,
                                                   15, 16,  17,    18, 19, 20,  21, 250};

    const std::vector<uint8_t> expected{kVectorPayloadBytes,
                                        kSingleFrameTransaction,
                                        0x00,
                                        0x3C,
                                        0x03,
                                        0x20,
                                        0xFF,
                                        0xFF,
                                        0x00,
                                        0x01,
                                        0x00,
                                        0x00,
                                        0x01,
                                        0x2C,
                                        0x00,
                                        0x0C,
                                        0x00,
                                        0x0D,
                                        0x00,
                                        0x0E,
                                        0x00,
                                        0x0F,
                                        0x00,
                                        0x10,
                                        0x00,
                                        0x11,
                                        0x00,
                                        0x12,
                                        0x00,
                                        0x13,
                                        0x00,
                                        0x14,
                                        0x00,
                                        0x15,
                                        0x00,
                                        0xFA};
    const auto encoded = encode_u16_vector(values);
    REQUIRE(encoded.size() == 36);
    REQUIRE(encoded == expected);
}

// 周期上报配置：长度 9、开关位、大端周期、大端关节掩码，共 11 字节。
TEST_CASE("comm: encode_periodic_config encodes period and mask", "[comm][l30][protocol]") {
    const std::vector<uint8_t> expected{0x09, 0x00, 0x01, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00};
    const auto encoded = encode_periodic_config(true, 20, 0);
    REQUIRE(encoded.size() == 11);
    REQUIRE(encoded == expected);

    const std::vector<uint8_t> masked{0x09, 0x00, 0x01, 0x00, 0x00, 0x02, 0x58, 0x00, 0x01, 0x00, 0x05};
    REQUIRE(encode_periodic_config(true, 600, 0x00010005U) == masked);

    const std::vector<uint8_t> longest{0x09, 0x00, 0x01, 0x00, 0x09, 0x27, 0xC0, 0x00, 0x01, 0xFF, 0xFF};
    REQUIRE(encode_periodic_config(true, 600000, kJointMaskLimit) == longest);
}

// 使能时周期必须落在 20..600000，掩码不得超过 17 位。
TEST_CASE("comm: encode_periodic_config rejects bad period and mask", "[comm][l30][protocol]") {
    REQUIRE_THROWS_AS(encode_periodic_config(true, 19, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(encode_periodic_config(true, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(encode_periodic_config(true, 600001, 0), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(encode_periodic_config(true, 20, 0));
    REQUIRE_NOTHROW(encode_periodic_config(true, 600000, 0));

    REQUIRE_THROWS_AS(encode_periodic_config(true, 20, 1U << kJointCount), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(encode_periodic_config(true, 20, kJointMaskLimit));

    const auto period = invalid_argument_message([] { (void)encode_periodic_config(true, 19, 0); });
    REQUIRE(period.find("period_ms") != std::string::npos);
    const auto mask = invalid_argument_message([] { (void)encode_periodic_config(true, 20, 1U << kJointCount); });
    REQUIRE(mask.find("joint_mask") != std::string::npos);
}

// 关闭上报时无视传入的周期与掩码，强制发全零形式，且不再做范围校验。
TEST_CASE("comm: encode_periodic_config forces zeros when disabled", "[comm][l30][protocol]") {
    const std::vector<uint8_t> expected{0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    REQUIRE(encode_periodic_config(false, 0, 0) == expected);
    REQUIRE(encode_periodic_config(false, 20, 0x0000FFFFU) == expected);
    REQUIRE_NOTHROW(encode_periodic_config(false, 19, 1U << kJointCount));
    REQUIRE(encode_periodic_config(false, 19, 1U << kJointCount) == expected);
}

// i16/u8 应答的正常路径：数据从 BYTE3 起，i16 按大端还原含负值。
TEST_CASE("comm: decode_i16_response and decode_u8_response read the payload", "[comm][l30][protocol]") {
    std::vector<uint8_t> frame{kVectorPayloadBytes, kSingleFrameTransaction, kStatusOk};
    for (size_t i = 0; i < kJointCount; ++i) {
        const auto value = static_cast<uint16_t>(static_cast<int16_t>(i) - 8);
        frame.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
        frame.push_back(static_cast<uint8_t>(value & 0xFFU));
    }
    const auto values = decode_i16_response(frame.data(), frame.size());
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(values[i] == static_cast<int16_t>(i) - 8);
    }

    std::vector<uint8_t> u8_frame{kU8VectorPayloadBytes, kSingleFrameTransaction, kStatusOk};
    for (size_t i = 0; i < kJointCount; ++i) {
        u8_frame.push_back(static_cast<uint8_t>(30 + i));
    }
    const auto temps = decode_u8_response(u8_frame.data(), u8_frame.size());
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(temps[i] == static_cast<uint8_t>(30 + i));
    }
}

// 声明长度不符、实际字节不够、单帧应答带非零事务码、短到读不到状态字，四类畸形都要拒。
TEST_CASE("comm: decode_i16_response rejects malformed framing", "[comm][l30][protocol]") {
    const auto short_frame = i16_frame(kVectorPayloadBytes, kSingleFrameTransaction, kStatusOk, 0);
    REQUIRE(short_frame.size() == 3);
    const auto too_short =
        protocol_error_message([&] { (void)decode_i16_response(short_frame.data(), short_frame.size()); });
    REQUIRE(too_short.find("response payload too short") != std::string::npos);

    const auto wrong_length = i16_frame(0x20, kSingleFrameTransaction, kStatusOk, kVectorPayloadBytes);
    const auto mismatch =
        protocol_error_message([&] { (void)decode_i16_response(wrong_length.data(), wrong_length.size()); });
    REQUIRE(mismatch.find("response length mismatch") != std::string::npos);
    REQUIRE(mismatch.find("34") != std::string::npos);
    REQUIRE(mismatch.find("32") != std::string::npos);

    const auto multi_frame = i16_frame(kVectorPayloadBytes, 0x10, kStatusOk, kVectorPayloadBytes);
    const auto transaction =
        protocol_error_message([&] { (void)decode_i16_response(multi_frame.data(), multi_frame.size()); });
    REQUIRE(transaction.find("unsupported transaction control") != std::string::npos);
    REQUIRE(transaction.find("0x10") != std::string::npos);

    const std::vector<uint8_t> stub{kVectorPayloadBytes, kSingleFrameTransaction};
    const auto headless = protocol_error_message([&] { (void)decode_i16_response(stub.data(), stub.size()); });
    REQUIRE(headless.find("response too short for status byte") != std::string::npos);

    REQUIRE_THROWS_AS(decode_i16_response(nullptr, 0), ProtocolError);
}

// 状态字先于长度校验：错误 ACK 的 BYTE0 是 0，报的必须是状态码而非长度不符。
TEST_CASE("comm: non-zero status is reported before the length mismatch", "[comm][l30][protocol]") {
    for (const uint8_t status : {0x10, 0x20, 0x23, 0x35, 0xF0}) {
        const auto frame = ack_payload(status);
        const auto message = protocol_error_message([&] { (void)decode_i16_response(frame.data(), frame.size()); });
        REQUIRE(message.find("L30 status") != std::string::npos);
        // 0x10 的文案本身含 "length mismatch"，所以只能钉住组帧那条前缀。
        REQUIRE(message.find("response length mismatch") == std::string::npos);
        REQUIRE(protocol_error_status([&] { (void)decode_i16_response(frame.data(), frame.size()); }) ==
                static_cast<int>(status));
        REQUIRE(protocol_error_status([&] { (void)decode_u8_response(frame.data(), frame.size()); }) ==
                static_cast<int>(status));
    }

    const auto refused = ack_payload(0x20);
    const auto text = protocol_error_message([&] { (void)decode_i16_response(refused.data(), refused.size()); });
    REQUIRE(text.find("0x20") != std::string::npos);
    REQUIRE(text.find("not unlocked") != std::string::npos);

    // 纯组帧错误不带设备状态，status() 保持 kStatusOk。
    const auto stub = std::vector<uint8_t>{kVectorPayloadBytes, kSingleFrameTransaction};
    REQUIRE(protocol_error_status([&] { (void)decode_i16_response(stub.data(), stub.size()); }) ==
            static_cast<int>(kStatusOk));
}

// 干净 ACK 不抛；status 非零抛，且 status() 可读回。
TEST_CASE("comm: check_response_status only throws on a device status", "[comm][l30][protocol]") {
    const auto ok = ack_payload();
    REQUIRE_NOTHROW(check_response_status(ok.data(), ok.size()));

    const auto bad = ack_payload(0x20);
    REQUIRE_THROWS_AS(check_response_status(bad.data(), bad.size()), ProtocolError);
    REQUIRE(protocol_error_status([&] { check_response_status(bad.data(), bad.size()); }) == 0x20);

    // 带负载的应答对 check_response_status 而言是长度不符（它期望 0 字节）。
    const auto padded = i16_frame(0x02, kSingleFrameTransaction, kStatusOk, 2);
    REQUIRE_THROWS_AS(check_response_status(padded.data(), padded.size()), ProtocolError);
}

// decode_response_payload 只返回声明的那段负载，尾随 FD 填充不带出。
TEST_CASE("comm: decode_response_payload returns the declared bytes", "[comm][l30][protocol]") {
    std::vector<uint8_t> frame{0x04, kSingleFrameTransaction, kStatusOk, 0xAA, 0xBB, 0xCC, 0xDD};
    frame.resize(frame.size() + 5, 0x00);
    const auto payload = decode_response_payload(frame.data(), frame.size(), 4);
    REQUIRE(payload == std::vector<uint8_t>{0xAA, 0xBB, 0xCC, 0xDD});

    REQUIRE_THROWS_AS(decode_response_payload(frame.data(), frame.size(), 5), ProtocolError);
}

// ASCII 应答以 BYTE0 为准取长度并剥掉尾部 NUL；>0x7F 的字节在这里不校验
// （拒收发生在 version.cpp 的 decode_ascii_byte 里）。
TEST_CASE("comm: decode_ascii_response trusts BYTE0 and strips trailing NULs", "[comm][l30][protocol]") {
    const std::vector<uint8_t> text{0x05, kSingleFrameTransaction, kStatusOk, 'L', '3', '0', 0x00, 0x00};
    REQUIRE(decode_ascii_response(text.data(), text.size()) == "L30");

    const std::vector<uint8_t> exact{0x03, kSingleFrameTransaction, kStatusOk, 'V', '6', '.'};
    REQUIRE(decode_ascii_response(exact.data(), exact.size()) == "V6.");

    const std::vector<uint8_t> empty{0x00, kSingleFrameTransaction, kStatusOk};
    REQUIRE(decode_ascii_response(empty.data(), empty.size()).empty());

    // 高位字节原样带出，不抛异常。
    const std::vector<uint8_t> high{0x02, kSingleFrameTransaction, kStatusOk, 0xC3, 'A'};
    const auto decoded = decode_ascii_response(high.data(), high.size());
    REQUIRE(decoded.size() == 2);
    REQUIRE(static_cast<uint8_t>(decoded[0]) == 0xC3);
    REQUIRE(decoded[1] == 'A');

    const std::vector<uint8_t> truncated{0x08, kSingleFrameTransaction, kStatusOk, 'L', '3', '0'};
    REQUIRE_THROWS_AS(decode_ascii_response(truncated.data(), truncated.size()), ProtocolError);
    REQUIRE_THROWS_AS(decode_ascii_response(nullptr, 0), ProtocolError);
}

// 周期上报没有状态字：数据从 BYTE2 起，掩码 0 表示全 17 个关节。
TEST_CASE("comm: decode_periodic_i16_report starts data at BYTE2", "[comm][l30][protocol]") {
    std::vector<uint8_t> frame{kVectorPayloadBytes, kSingleFrameTransaction};
    for (size_t i = 0; i < kJointCount; ++i) {
        const auto value = static_cast<uint16_t>(static_cast<int16_t>(i) * -100);
        frame.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
        frame.push_back(static_cast<uint8_t>(value & 0xFFU));
    }
    const auto values = decode_periodic_i16_report(frame.data(), frame.size(), 0);
    REQUIRE(values.size() == kJointCount);
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(values[i] == static_cast<int16_t>(i) * -100);
    }

    // 部分掩码只报被选中的关节数量，长度按 popcount*2 声明。
    const std::vector<uint8_t> partial{0x06, kSingleFrameTransaction, 0x00, 0x64, 0xFF, 0x38, 0xFC, 0x18};
    const auto picked = decode_periodic_i16_report(partial.data(), partial.size(), 0x00010005U);
    REQUIRE(picked == std::vector<int16_t>{100, -200, -1000});
}

// u8 周期上报同样从 BYTE2 起，长度直接是关节数。
TEST_CASE("comm: decode_periodic_u8_report starts data at BYTE2", "[comm][l30][protocol]") {
    std::vector<uint8_t> frame{kU8VectorPayloadBytes, kSingleFrameTransaction};
    for (size_t i = 0; i < kJointCount; ++i) {
        frame.push_back(static_cast<uint8_t>(40 + i));
    }
    const auto values = decode_periodic_u8_report(frame.data(), frame.size(), 0);
    REQUIRE(values.size() == kJointCount);
    REQUIRE(values.front() == 40);
    REQUIRE(values.back() == static_cast<uint8_t>(40 + kJointCount - 1));

    const std::vector<uint8_t> partial{0x02, kSingleFrameTransaction, 0x37, 0x38};
    REQUIRE(decode_periodic_u8_report(partial.data(), partial.size(), 0x03U) == std::vector<uint8_t>{0x37, 0x38});
}

// 周期上报的长度不符与字节不够都要拒，且报文里不掺状态字概念。
TEST_CASE("comm: periodic reports reject length mismatch and short payloads", "[comm][l30][protocol]") {
    std::vector<uint8_t> wrong_length{0x20, kSingleFrameTransaction};
    wrong_length.resize(2 + kVectorPayloadBytes, 0x00);
    const auto mismatch =
        protocol_error_message([&] { (void)decode_periodic_i16_report(wrong_length.data(), wrong_length.size(), 0); });
    REQUIRE(mismatch.find("periodic report length mismatch") != std::string::npos);

    const std::vector<uint8_t> truncated{kVectorPayloadBytes, kSingleFrameTransaction, 0x00, 0x01, 0x00, 0x02};
    const auto short_payload =
        protocol_error_message([&] { (void)decode_periodic_i16_report(truncated.data(), truncated.size(), 0); });
    REQUIRE(short_payload.find("periodic report payload too short") != std::string::npos);

    const std::vector<uint8_t> u8_wrong{0x05, kSingleFrameTransaction, 0x01, 0x02, 0x03, 0x04, 0x05};
    REQUIRE_THROWS_AS(decode_periodic_u8_report(u8_wrong.data(), u8_wrong.size(), 0), ProtocolError);
    const std::vector<uint8_t> u8_short{kU8VectorPayloadBytes, kSingleFrameTransaction, 0x01};
    REQUIRE_THROWS_AS(decode_periodic_u8_report(u8_short.data(), u8_short.size(), 0), ProtocolError);

    REQUIRE_THROWS_AS(decode_periodic_i16_report(nullptr, 0, 0), ProtocolError);
    REQUIRE_THROWS_AS(decode_periodic_u8_report(nullptr, 0, 0), ProtocolError);
}

// 触觉 61 + 11 拼成 72 字节；0x11 帧尾部的 FD 填充要被忽略。
TEST_CASE("comm: assemble_tactile_payload joins the 61 and 11 byte frames", "[comm][l30][protocol]") {
    // 0x10 帧的 3 字节头加 61 字节数据正好是 64，已到 CAN FD 上限，无填充余地。
    const auto first = tactile_frame(0x10, kTactileFirstLength, 0, 0);
    const auto second = tactile_frame(0x11, kTactileSecondLength, kTactileFirstLength, 0);
    REQUIRE(first.size() == 64);
    REQUIRE(second.size() == 14);

    const auto matrix = assemble_tactile_payload(first, second);
    REQUIRE(matrix.size() == kTactileDataLength);
    for (size_t i = 0; i < kTactileDataLength; ++i) {
        REQUIRE(matrix[i] == static_cast<uint8_t>(i));
    }

    // 0x11 帧实际会被填充到 16 字节，多出来的零不能进结果。
    const auto padded = tactile_frame(0x11, kTactileSecondLength, kTactileFirstLength, 2);
    REQUIRE(padded.size() == 16);
    REQUIRE(assemble_tactile_payload(first, padded) == matrix);
}

// 触觉帧同样先看状态字再看事务码：事务码互换要报 mismatch，错误 ACK 要报状态码。
TEST_CASE("comm: assemble_tactile_payload rejects swapped markers and bad status", "[comm][l30][protocol]") {
    const auto swapped_first = tactile_frame(0x11, kTactileFirstLength, 0, 0);
    const auto swapped_second = tactile_frame(0x10, kTactileSecondLength, kTactileFirstLength, 2);
    const auto mismatch =
        protocol_error_message([&] { (void)assemble_tactile_payload(swapped_first, swapped_second); });
    REQUIRE(mismatch.find("tactile transaction mismatch") != std::string::npos);
    REQUIRE(mismatch.find("expected 0x10") != std::string::npos);
    REQUIRE(mismatch.find("got 0x11") != std::string::npos);

    const auto first = tactile_frame(0x10, kTactileFirstLength, 0, 0);
    const auto refused = ack_payload(0x20);
    REQUIRE(protocol_error_status([&] { (void)assemble_tactile_payload(refused, refused); }) == 0x20);
    const auto status_text = protocol_error_message([&] { (void)assemble_tactile_payload(refused, refused); });
    REQUIRE(status_text.find("L30 tactile status") != std::string::npos);
    REQUIRE(status_text.find("transaction mismatch") == std::string::npos);

    // 长度字段不符与字节不够分别有各自的文案。
    const auto wrong_length = tactile_frame(0x10, kTactileFirstLength, 0, 0);
    std::vector<uint8_t> relabelled = wrong_length;
    relabelled[0] = 0x20;
    const auto length_text = protocol_error_message([&] {
        (void)assemble_tactile_payload(relabelled, tactile_frame(0x11, kTactileSecondLength, kTactileFirstLength, 0));
    });
    REQUIRE(length_text.find("tactile length mismatch") != std::string::npos);

    const std::vector<uint8_t> stub{static_cast<uint8_t>(kTactileFirstLength), 0x10};
    REQUIRE_THROWS_AS(assemble_tactile_payload(stub, stub), ProtocolError);

    std::vector<uint8_t> truncated{static_cast<uint8_t>(kTactileFirstLength), 0x10, kStatusOk, 0x01, 0x02};
    const auto short_text = protocol_error_message([&] {
        (void)assemble_tactile_payload(truncated, tactile_frame(0x11, kTactileSecondLength, kTactileFirstLength, 0));
    });
    REQUIRE(short_text.find("tactile frame payload too short") != std::string::npos);
    REQUIRE_NOTHROW(assemble_tactile_payload(first, tactile_frame(0x11, kTactileSecondLength, kTactileFirstLength, 0)));
}

// 协议里定义的每个状态码都要有对应文案，未定义的落到 unknown status。
TEST_CASE("comm: status_message covers every documented code", "[comm][l30][protocol]") {
    REQUIRE(status_message(0x00) == "success");
    REQUIRE(status_message(0x10) == "DLC or data length mismatch");
    REQUIRE(status_message(0x11) == "parameter out of range or invalid");
    REQUIRE(status_message(0x12) == "unsupported command code");
    REQUIRE(status_message(0x13) == "invalid subcommand");
    REQUIRE(status_message(0x14) == "data format error");
    REQUIRE(status_message(0x20) == "insufficient permission, not unlocked, or wrong password");
    REQUIRE(status_message(0x21) == "zero point not calibrated");
    REQUIRE(status_message(0x22) == "motor not enabled");
    REQUIRE(status_message(0x23) == "operation not allowed in the current state");
    REQUIRE(status_message(0x24) == "not configured (hand type or zero point uninitialized)");
    REQUIRE(status_message(0x30) == "motor data read error");
    REQUIRE(status_message(0x31) == "motor data write error");
    REQUIRE(status_message(0x32) == "multi-frame transfer timeout");
    REQUIRE(status_message(0x33) == "incomplete multi-frame data");
    REQUIRE(status_message(0x34) == "periodic report configuration failed");
    REQUIRE(status_message(0x35) == "reserved");
    REQUIRE(status_message(0x40) == "communication timeout");
    REQUIRE(status_message(0x41) == "communication lost");
    REQUIRE(status_message(0xF0) == "system busy");
    REQUIRE(status_message(0x99) == "unknown status");
    REQUIRE(status_message(0xFF) == "unknown status");
}

// 掩码 0 代表全 17 个关节，否则取 popcount；17 位以上的比特不计入。
TEST_CASE("comm: reported_joint_count is popcount or all joints", "[comm][l30][protocol]") {
    REQUIRE(reported_joint_count(0) == kJointCount);
    REQUIRE(reported_joint_count(0x00000001U) == 1);
    REQUIRE(reported_joint_count(0x00000005U) == 2);
    REQUIRE(reported_joint_count(0x0000001FU) == 5);
    REQUIRE(reported_joint_count(0x00010005U) == 3);
    REQUIRE(reported_joint_count(kJointMaskLimit) == kJointCount);
    REQUIRE(reported_joint_count(1U << 20U) == 0);
}

TEST_CASE("comm: L30 writes expose semantic command families", "[comm][l30][protocol][health]") {
    using namespace linkerbot::hand::l30::protocol;
    const auto family = [](uint8_t parent, uint8_t subcmd) {
        return request_plan(request_frame(kNodeId, kHostId, parent, subcmd, kAccessWrite, empty_request_payload(),
                                          kEmptyRequestBytes))
            .command_family;
    };
    REQUIRE(family(kParentControl, kSubPosition) == "angle_targets");
    REQUIRE(family(kParentControl, kSubCurrentLimit) == "current_limits");
    REQUIRE(family(kParentControl, kSubSpeed) == "speed_targets");
    REQUIRE(family(kParentControl, kSubFault) == "fault_reset");
    REQUIRE(family(kParentControl, kSubEnable) == "configuration");
    REQUIRE(family(kParentConfig, kSubConfigUnlock) == "configuration");
    REQUIRE(family(kParentQuery, kSubTemperature) == "l30_writes");
}

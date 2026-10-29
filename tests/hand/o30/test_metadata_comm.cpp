#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o30/protocol.hpp>
#include <linkerbot/hand/o30/sensor.hpp>

#include "hand/o30/comm_helpers.hpp"

using linkerbot::hand::o30::protocol::kMiCommError;
using linkerbot::hand::o30::protocol::kMiProductInfo;
using linkerbot::hand::o30::protocol::kMiSensorCh1;
using linkerbot::hand::o30::protocol::kMiSensorInfo;
using linkerbot::hand::o30::protocol::kProductInfoBytes;
using linkerbot::hand::o30::protocol::ProtocolError;

namespace {
    constexpr auto kTimeout = o30_comm::kTimeout;
} // namespace

// FakeTransport 元数据：0x41 产品信息分片、0x31 传感器、0x4F 错误历史。

// 完整 225 字节产品信息一次读齐并按协议字段切开。
TEST_CASE("comm: product info reads and reassembles 225 bytes", "[comm][o30][metadata]") {
    auto h = o30_comm::make_hand();
    const auto info = h.hand->version.get_product_info(kTimeout);

    REQUIRE(info.model == "O30");
    REQUIRE(info.protocol_name == "HOP");
    REQUIRE(info.protocol_version == "0.0.3");
    REQUIRE(info.app_version == "1.1.0");
    REQUIRE(info.supported_interfaces == "CAN,UART");
    REQUIRE(o30_comm::payloads(h.transport).front() == std::vector<uint8_t>({kMiProductInfo, 0x00, kProductInfoBytes}));
}

// 0.0.4 可采用旧布局；真正新布局的板号占 16 字节，末尾侧别在 F1。
TEST_CASE("comm: product info detects both Python HOP layouts", "[comm][o30][metadata]") {
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(kMiProductInfo, 0x61, 8, "0.0.4");
    SECTION("deployed layout despite new version") {
        REQUIRE(h.hand->version.get_product_info(kTimeout).app_version == "1.1.0");
    }
    SECTION("date probe selects deployed layout") {
        h.hop->put_ascii(kMiProductInfo, 0x71, 8, "");
        REQUIRE(h.hand->version.get_product_info(kTimeout).app_version == "1.1.0");
    }
    SECTION("documented layout") {
        h.hop->put_ascii(kMiProductInfo, 0x69, 16, "V2.1.0");
        h.hop->put_ascii(kMiProductInfo, 0x79, 16, "V3.2.0");
        h.hop->put_ascii(kMiProductInfo, 0x89, 16, "V4.3.0");
        h.hop->put_ascii(kMiProductInfo, 0x99, 8, "V1.0.0");
        h.hop->put_ascii(kMiProductInfo, 0xA1, 8, "V5.4.0");
        h.hop->put_ascii(kMiProductInfo, 0xA9, 8, "M1.0.0");
        h.hop->put_ascii(kMiProductInfo, 0xB1, 32, "2026-09-16");
        h.hop->put_ascii(kMiProductInfo, 0xD1, 32, "CAN,UART");
        h.hop->put_ascii(kMiProductInfo, 0xF1, 8, "right");
        const auto info = h.hand->version.get_product_info(kTimeout);
        REQUIRE(info.interface_hw_version == "V2.1.0");
        REQUIRE(info.adapter_hw_version == "V3.2.0");
        REQUIRE(info.control_hw_version == "V4.3.0");
        REQUIRE(info.bootloader_version == "V1.0.0");
        REQUIRE(info.app_version == "V5.4.0");
        REQUIRE(info.hand_side == "right");
        REQUIRE(info.build_time == "2026-09-16");
    }
}

// 单字段 ASCII 读取走对应 SI。
TEST_CASE("comm: version helpers read individual ASCII fields", "[comm][o30][metadata]") {
    auto h = o30_comm::make_hand();
    REQUIRE(h.hand->version.get_model(kTimeout) == "O30");
    REQUIRE(h.hand->version.get_protocol_name(kTimeout) == "HOP");
    REQUIRE(h.hand->version.get_protocol_version(kTimeout) == "0.0.3");
    REQUIRE(h.hand->version.get_device_uid(kTimeout) == "DEVICE-UID-O30");
}

// NO_SENSOR 且 total_length=0 时不要去读 0x32-0x34。
TEST_CASE("comm: sensor info reports no sensor without reading data channels", "[comm][o30][metadata]") {
    auto h = o30_comm::make_hand();
    const auto info = h.hand->sensor.get_info(kTimeout);

    REQUIRE(info.type == "NO_SENSOR");
    REQUIRE(info.total_length == 0);
    REQUIRE(info.rows == 0);
    REQUIRE(info.columns == 0);
    const auto sent = o30_comm::payloads(h.transport);
    REQUIRE_FALSE(sent.empty());
    REQUIRE(sent.front() == std::vector<uint8_t>({0x41, 0x61, 8}));
    REQUIRE(sent[1] == std::vector<uint8_t>({kMiSensorInfo, 0x00, 0x20}));
    for (size_t i = 1; i < sent.size(); ++i) {
        REQUIRE(sent[i][0] == kMiSensorInfo);
    }
}

// 有矩阵形状时按小端解析总长、行、列。
TEST_CASE("comm: sensor info decodes shape and little-endian length", "[comm][o30][metadata]") {
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(kMiSensorInfo, 0x00, 0x20, "TACTILE");
    h.hop->put_ascii(kMiSensorInfo, 0x20, 0x08, "raw");
    auto &object = h.hop->objects[{kMiSensorInfo, false}];
    object.resize(50, 0);
    object[0x2B] = 0x34;
    object[0x2C] = 0x12;
    object[0x2D] = 2;
    object[0x2E] = 12;
    object[0x2F] = 6;

    const auto info = h.hand->sensor.get_info(kTimeout);
    REQUIRE(info.type == "TACTILE");
    REQUIRE(info.unit == "raw");
    REQUIRE(info.total_length == 0x1234);
    REQUIRE(info.selected == 2);
    REQUIRE(info.rows == 12);
    REQUIRE(info.columns == 6);
}

// total_length 为 0 时 read_channel 抛 ProtocolError。
TEST_CASE("comm: sensor channel read is skipped when total_length is 0", "[comm][o30][metadata]") {
    auto h = o30_comm::make_hand();
    REQUIRE_THROWS_AS(h.hand->sensor.read_channel(1, kTimeout), ProtocolError);
    REQUIRE_THROWS_AS(h.hand->sensor.read_channel(0, kTimeout), linkerbot::hand::ValidationError);
}

// 总长非 0 才读通道，MI 0x32。
TEST_CASE("comm: sensor channel read uses MI 0x32 when length is non-zero", "[comm][o30][metadata]") {
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(kMiSensorInfo, 0, 32, "TACTILE");
    auto &info = h.hop->objects[{kMiSensorInfo, false}];
    info.resize(50, 0);
    info[0x2B] = 4;
    info[0x2C] = 0;
    h.hop->put(kMiSensorCh1, {0xAA, 0xBB, 0xCC, 0xDD});

    const auto data = h.hand->sensor.read_channel(1, kTimeout);
    REQUIRE(data.channel == 1);
    REQUIRE(data.payload.size() == 4);
    REQUIRE(data.payload[0] == 0xAA);
}

// 主动读 0x4F 是错误历史对象，不是失败应答。
TEST_CASE("comm: diagnostics reads 4F as a normal object", "[comm][o30][metadata]") {
    auto h = o30_comm::make_hand();
    std::vector<uint8_t> errors(17, 0);
    for (uint8_t i = 0; i < 17; ++i) {
        errors[i] = i;
    }
    h.hop->put(kMiCommError, errors);

    REQUIRE(h.hand->comm_error.get_latest_index(kTimeout) == 0);
    const auto history = h.hand->comm_error.get_history(kTimeout);
    REQUIRE(history.latest_index == 0);
    for (uint8_t i = 0; i < 16; ++i) {
        REQUIRE(history.history[i] == static_cast<uint8_t>(i + 1));
    }
    const auto sent = o30_comm::payloads(h.transport);
    REQUIRE(sent[0] == std::vector<uint8_t>({0x4F, 0x00, 0x01}));
}

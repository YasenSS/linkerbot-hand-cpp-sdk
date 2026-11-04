#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>
#include <linkerbot/hand/l30/version.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::hand_side_name;
using linkerbot::hand::l30::HandSide;
using linkerbot::hand::l30::kHandTypeLeft;
using linkerbot::hand::l30::kHandTypeRight;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::Version;
using linkerbot::hand::l30::protocol::ProtocolError;

namespace {

    namespace protocol = linkerbot::hand::l30::protocol;

    constexpr auto kTimeout = std::chrono::milliseconds{500};

    // 实测过的 config 父命令查询 ID（DstID=1、SrcID=0）。
    constexpr uint32_t kDeviceInfoId = 0x00604100;
    constexpr uint32_t kProductCodeId = 0x00606100;
    constexpr uint32_t kNodeIdId = 0x00608100;
    constexpr uint32_t kHandTypeId = 0x0060C100;

    // 18 字节 DeviceInfo：产品号 0，串号 1–4 大端 u32，软件 5–7，硬件 8–10，
    // 结构 11–13，节点号 14，左右手 15，传感器型号 16，产地 17。
    std::vector<uint8_t> device_info_bytes(uint8_t hand_type = kHandTypeLeft, uint8_t sensor_type = 'B',
                                           uint8_t origin = 'A') {
        return {0x13, 0x00, 0x0A, 0xBC, 0xDE, 1, 0, 6, 2, 1, 0, 3, 4, 5, 7, hand_type, sensor_type, origin};
    }

    struct InstallVersionHandlersArguments {
        FakeTransport *transport;
        const std::vector<uint8_t> &info;
        const std::vector<uint8_t> &product_code;
        uint8_t node_id;
        uint8_t hand_type;
    };
    void install_version_handlers(const InstallVersionHandlersArguments &arguments) {
        arguments.transport->on_request(kDeviceInfoId, l30_comm::payload_handler(arguments.info));
        arguments.transport->on_request(kProductCodeId, l30_comm::payload_handler(arguments.product_code));
        arguments.transport->on_request(kNodeIdId, l30_comm::payload_handler({arguments.node_id}));
        arguments.transport->on_request(kHandTypeId, l30_comm::payload_handler({arguments.hand_type}));
    }

    std::vector<uint8_t> ascii_bytes(const std::string &text) { return {text.begin(), text.end()}; }

    // 仓库不用 Catch2 matcher，异常文本一律自己抓出来 find()。
    bool throws_with(const std::function<void()> &call, const std::string &needle) {
        try {
            call();
        } catch (const std::exception &error) {
            return std::string(error.what()).find(needle) != std::string::npos;
        }
        return false;
    }

    int thrown_status(const std::function<void()> &call) {
        try {
            call();
        } catch (const ProtocolError &error) {
            return static_cast<int>(error.status());
        }
        return -1;
    }

} // namespace

// FakeTransport 设备身份：config 父命令（0x3）下的四个只读查询。DeviceInfo 是定长
// 18 字节结构，产品码是变长 ASCII，节点号与左右手各一个字节；全部无需先解锁配置。

// 可选校验必须匹配产品号、节点、侧别以及产品码里的全部身份字段。
TEST_CASE("comm: L30 verified identity rejects mismatches", "[comm][l30][identity]") {
    using linkerbot::hand::IdentityMismatchError;
    using linkerbot::hand::IdentitySide;
    auto info = device_info_bytes();
    info[14] = l30_comm::kNodeId;
    std::string code = "LHT30-06-703710-L-B-3-A";
    bool mismatch = false;
    SECTION("valid") {}
    SECTION("wrong product id") {
        info[0] = 0x12;
        mismatch = true;
    }
    SECTION("wrong node id") {
        info[14] = 7;
        mismatch = true;
    }
    SECTION("wrong side") {
        info[15] = kHandTypeRight;
        mismatch = true;
    }
    SECTION("wrong serial") {
        code = "LHT30-06-2-L-B-3-A";
        mismatch = true;
    }
    SECTION("wrong sensor") {
        code = "LHT30-06-703710-L-Z-3-A";
        mismatch = true;
    }
    SECTION("wrong model") {
        code = "LHL6-06-703710-L-B-3-A";
        mismatch = true;
    }
    auto transport = std::make_unique<FakeTransport>();
    install_version_handlers({transport.get(), info, ascii_bytes(code), l30_comm::kNodeId, kHandTypeLeft});
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    const auto read = [&]() { return hand.version.get_verified_device_info(kTimeout, IdentitySide::kLeft); };
    if (mismatch) {
        REQUIRE_THROWS_AS(read(), IdentityMismatchError);
    } else {
        REQUIRE(read().product_id == 0x13);
    }
}

// 18 字节结构逐字段解出来：产品号、大端串号、三个版本号、节点号、左右手、传感器型号、产地。
TEST_CASE("comm: device info decodes all eighteen bytes", "[comm][l30][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kDeviceInfoId, l30_comm::payload_handler(device_info_bytes()));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    const auto info = hand.version.get_device_info(kTimeout);
    REQUIRE(raw->sent().size() == 1);
    REQUIRE(raw->sent().front().id == kDeviceInfoId);
    REQUIRE(info.product_id == 0x13);
    REQUIRE(info.serial_number == 0x000ABCDEU);
    REQUIRE(info.software_version.major == 1);
    REQUIRE(info.software_version.minor == 0);
    REQUIRE(info.software_version.patch == 6);
    REQUIRE(info.hardware_version.major == 2);
    REQUIRE(info.hardware_version.minor == 1);
    REQUIRE(info.hardware_version.patch == 0);
    REQUIRE(info.structure_version.major == 3);
    REQUIRE(info.structure_version.minor == 4);
    REQUIRE(info.structure_version.patch == 5);
    REQUIRE(info.node_id == 7);
    REQUIRE(info.hand_side == HandSide::kLeft);
    REQUIRE(info.sensor_type == 'B');
    REQUIRE(info.origin == 'A');
}

// 版本号格式化成 "V<主>.<次>.<修订>"。
TEST_CASE("comm: version to_string formats as V major dot minor dot patch", "[comm][l30][version]") {
    REQUIRE(Version{1, 0, 6}.to_string() == std::string("V1.0.6"));
    REQUIRE(Version{}.to_string() == std::string("V0.0.0"));
    REQUIRE(Version{12, 34, 56}.to_string() == std::string("V12.34.56"));
}

// 左右手名字；越界枚举值报 unknown L30 hand side。
TEST_CASE("comm: hand side name covers left and right", "[comm][l30][version]") {
    REQUIRE(hand_side_name(HandSide::kLeft) == "left");
    REQUIRE(hand_side_name(HandSide::kRight) == "right");

    // Fixed-underlying-type enum values are valid C++17, including unknown values and bit masks.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto bogus = static_cast<HandSide>(7);
    REQUIRE_THROWS_AS(hand_side_name(bogus), linkerbot::hand::ValidationError);
    REQUIRE(throws_with([bogus] { (void)hand_side_name(bogus); }, "unknown L30 hand side"));
}

// hand_type 字节 0 是左手、1 是右手。
TEST_CASE("comm: device info hand type byte selects the side", "[comm][l30][version]") {
    auto left_transport = std::make_unique<FakeTransport>();
    left_transport->on_request(kDeviceInfoId, l30_comm::payload_handler(device_info_bytes(kHandTypeLeft)));
    L30 left(std::move(left_transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    REQUIRE(left.version.get_device_info(kTimeout).hand_side == HandSide::kLeft);

    auto right_transport = std::make_unique<FakeTransport>();
    right_transport->on_request(kDeviceInfoId, l30_comm::payload_handler(device_info_bytes(kHandTypeRight)));
    L30 right(std::move(right_transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    REQUIRE(right.version.get_device_info(kTimeout).hand_side == HandSide::kRight);
}

// hand_type 既不是 0 也不是 1 时报 unknown L30 hand type。
TEST_CASE("comm: device info rejects an unknown hand type", "[comm][l30][version]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kDeviceInfoId, l30_comm::payload_handler(device_info_bytes(2)));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.version.get_device_info(kTimeout), ProtocolError);
    REQUIRE(throws_with([&hand] { (void)hand.version.get_device_info(kTimeout); }, "unknown L30 hand type 2"));
}

// 传感器型号或产地字节超过 0x7F 就不是合法 ASCII，报 not valid ASCII。
TEST_CASE("comm: device info rejects non-ASCII sensor type and origin", "[comm][l30][version]") {
    auto sensor_transport = std::make_unique<FakeTransport>();
    sensor_transport->on_request(kDeviceInfoId, l30_comm::payload_handler(device_info_bytes(kHandTypeLeft, 0x80, 'A')));
    L30 sensor_hand(std::move(sensor_transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    REQUIRE_THROWS_AS(sensor_hand.version.get_device_info(kTimeout), ProtocolError);
    REQUIRE(throws_with([&sensor_hand] { (void)sensor_hand.version.get_device_info(kTimeout); }, "not valid ASCII"));

    auto origin_transport = std::make_unique<FakeTransport>();
    origin_transport->on_request(kDeviceInfoId, l30_comm::payload_handler(device_info_bytes(kHandTypeLeft, 'B', 0xFF)));
    L30 origin_hand(std::move(origin_transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    REQUIRE_THROWS_AS(origin_hand.version.get_device_info(kTimeout), ProtocolError);
    REQUIRE(throws_with([&origin_hand] { (void)origin_hand.version.get_device_info(kTimeout); }, "not valid ASCII"));
}

// 产品码按 BYTE0 声明的长度解 ASCII，尾部的 NUL 要剥掉。
TEST_CASE("comm: product code strips trailing NULs", "[comm][l30][version]") {
    auto padded = ascii_bytes("LHT30-06-001-L-B-3-A");
    padded.push_back(0x00);
    padded.push_back(0x00);

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kProductCodeId, l30_comm::payload_handler(padded));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE(hand.version.get_product_code(kTimeout) == std::string("LHT30-06-001-L-B-3-A"));
    REQUIRE(raw->sent().size() == 1);
    REQUIRE(raw->sent().front().id == kProductCodeId);
}

// 节点号是单字节响应。
TEST_CASE("comm: node id read returns one byte", "[comm][l30][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kNodeIdId, l30_comm::payload_handler({0x0B}));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE(hand.version.get_node_id(kTimeout) == 0x0B);
    REQUIRE(raw->sent().size() == 1);
    REQUIRE(raw->sent().front().id == kNodeIdId);
}

// 单独的左右手查询走 0x0060C100，不经过 DeviceInfo。
TEST_CASE("comm: hand side read uses the standalone config query", "[comm][l30][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kHandTypeId, l30_comm::payload_handler({kHandTypeRight}));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE(hand.version.get_hand_side(kTimeout) == HandSide::kRight);
    REQUIRE(raw->sent().size() == 1);
    REQUIRE(raw->sent().front().id == kHandTypeId);
    REQUIRE(l30_comm::useful(raw->sent().front()) == std::vector<uint8_t>{0x00, 0x00});
    REQUIRE(raw->sent().front().dlc == protocol::kEmptyRequestBytes);
}

// 四个查询都在发送前校验超时，非正值一个字节都不许上总线。
TEST_CASE("comm: version reads reject non-positive timeout", "[comm][l30][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    install_version_handlers({raw, device_info_bytes(), ascii_bytes("LHT30"), 1, kHandTypeLeft});

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    constexpr auto kZero = std::chrono::milliseconds{0};
    constexpr auto kNegative = std::chrono::milliseconds{-5};
    REQUIRE_THROWS_AS(hand.version.get_device_info(kZero), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.version.get_product_code(kZero), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.version.get_node_id(kZero), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.version.get_hand_side(kNegative), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// DeviceInfo 声明的长度不足 18 字节时报长度不匹配。
TEST_CASE("comm: short device info payload throws", "[comm][l30][version]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kDeviceInfoId, l30_comm::payload_handler({0x13, 0x00, 0x0A, 0xBC, 0xDE, 1, 0, 6}));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.version.get_device_info(kTimeout), ProtocolError);
    REQUIRE(throws_with([&hand] { (void)hand.version.get_device_info(kTimeout); },
                        "response length mismatch: expected 18 bytes, got 8"));
}

// 四个查询各自的非零状态字都要原样带到 ProtocolError::status()。
TEST_CASE("comm: version reads propagate the device status", "[comm][l30][version]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kDeviceInfoId, l30_comm::payload_handler(device_info_bytes(), 0x24));
    raw->on_request(kProductCodeId, l30_comm::payload_handler(ascii_bytes("LHT30"), 0x12));
    raw->on_request(kNodeIdId, l30_comm::payload_handler({0x01}, 0x20));
    raw->on_request(kHandTypeId, l30_comm::payload_handler({kHandTypeLeft}, 0x23));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE(thrown_status([&hand] { (void)hand.version.get_device_info(kTimeout); }) == 0x24);
    REQUIRE(thrown_status([&hand] { (void)hand.version.get_product_code(kTimeout); }) == 0x12);
    REQUIRE(thrown_status([&hand] { (void)hand.version.get_node_id(kTimeout); }) == 0x20);
    REQUIRE(thrown_status([&hand] { (void)hand.version.get_hand_side(kTimeout); }) == 0x23);
}

#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l30/force_sensor.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l30::Finger;
using linkerbot::hand::l30::finger_name;
using linkerbot::hand::l30::finger_subcommand;
using linkerbot::hand::l30::FingerForceData;
using linkerbot::hand::l30::kAllFingers;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::protocol::ProtocolError;

namespace {

    namespace protocol = linkerbot::hand::l30::protocol;

    constexpr auto kTimeout = std::chrono::milliseconds{500};
    // 超时用例必须有界，取 50ms 让回归时立刻失败而不是挂住。
    constexpr auto kShortTimeout = std::chrono::milliseconds{50};

    // 实测过的五指触觉请求 ID（DstID=1、SrcID=0），顺序即拇指→小指。
    constexpr std::array<uint32_t, 5> kFingerRequestIds{0x00402100, 0x00404100, 0x00406100, 0x00408100, 0x0040A100};

    uint32_t tactile_request_id(Finger finger) {
        return l30_comm::request_id(protocol::kParentTactile, finger_subcommand(finger), protocol::kAccessRead);
    }

    void install_tactile_handlers(FakeTransport *transport,
                                  const std::array<uint8_t, protocol::kTactileDataLength> &matrix,
                                  uint8_t status = protocol::kStatusOk) {
        for (const auto finger : kAllFingers) {
            transport->on_request(tactile_request_id(finger), l30_comm::tactile_handler(matrix, status));
        }
    }

    // 第二帧尾部再补 extra 个零字节，模拟 CAN FD 的长度对齐填充。
    FakeTransport::Handler padded_tactile_handler(const std::array<uint8_t, protocol::kTactileDataLength> &matrix,
                                                  uint8_t extra) {
        return [matrix, extra](const Message &req) {
            auto frames = l30_comm::tactile_frames(matrix, protocol::expected_response_id(req.id));
            frames.back().dlc = static_cast<uint8_t>(frames.back().dlc + extra);
            return frames;
        };
    }

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

// FakeTransport 触觉矩阵：一次读发一帧空载荷请求，设备回 0x10/0x11 两帧共 72 字节，
// 按行主序拼成 12 行 × 6 列。每个手指有独立的 29 位仲裁 ID，所以处理器要按 ID 逐个注册。

// 五指请求各自命中实测 ID，载荷是 00 00、dlc=2 的扩展 FD 帧。
TEST_CASE("comm: tactile read emits one request id per finger", "[comm][l30][force]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    install_tactile_handlers(raw, l30_comm::tactile_ramp());

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    for (size_t i = 0; i < kAllFingers.size(); ++i) {
        raw->clear_sent();
        (void)hand.force_sensor.get_finger(kAllFingers.at(i), kTimeout);

        const auto sent = raw->sent();
        REQUIRE(sent.size() == 1);
        REQUIRE(sent.front().id == kFingerRequestIds.at(i));
        REQUIRE(l30_comm::useful(sent.front()) == std::vector<uint8_t>{0x00, 0x00});
        REQUIRE(sent.front().dlc == protocol::kEmptyRequestBytes);
        REQUIRE(sent.front().is_extended);
        REQUIRE(sent.front().is_fd);
    }
}

// 0..71 的斜坡载荷必须按行主序落位：values[row][col] == row*6+col，转置会立刻露出来。
TEST_CASE("comm: tactile two frames reassemble row-major", "[comm][l30][force]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    install_tactile_handlers(raw, l30_comm::tactile_ramp());

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = hand.force_sensor.get_finger(Finger::kIndex, kTimeout);
    REQUIRE(data.finger == Finger::kIndex);
    REQUIRE(FingerForceData::kRows == protocol::kTactileRows);
    REQUIRE(FingerForceData::kColumns == protocol::kTactileColumns);
    for (size_t row = 0; row < FingerForceData::kRows; ++row) {
        for (size_t column = 0; column < FingerForceData::kColumns; ++column) {
            REQUIRE(data.values.at(row).at(column) == static_cast<uint8_t>((row * FingerForceData::kColumns) + column));
        }
    }
    // 跨帧边界的两个字节（第 61、62 字节）也要落在正确的行列上。
    REQUIRE(data.values[10][0] == 60);
    REQUIRE(data.values[10][1] == 61);
    REQUIRE(data.values[11][5] == 71);
}

// 第二帧补 10 字节零填充后，72 字节的有效数据不受影响。
TEST_CASE("comm: tactile ignores CAN FD zero padding", "[comm][l30][force]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(tactile_request_id(Finger::kThumb), padded_tactile_handler(l30_comm::tactile_ramp(), 10));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = hand.force_sensor.get_finger(Finger::kThumb, kTimeout);
    REQUIRE(data.values[0][0] == 0);
    REQUIRE(data.values[10][1] == 61);
    REQUIRE(data.values[11][5] == 71);
}

// get_blocking 按拇指→食指→中指→无名指→小指的顺序读五指，并写入整机快照。
TEST_CASE("comm: tactile get_blocking reads five fingers in order", "[comm][l30][force]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    install_tactile_handlers(raw, l30_comm::tactile_ramp());

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    REQUIRE_FALSE(hand.force_sensor.get_snapshot().has_value());
    raw->clear_sent();

    const auto data = hand.force_sensor.get_blocking(kTimeout);
    REQUIRE(l30_comm::sent_ids(raw) == std::vector<uint32_t>{kFingerRequestIds.begin(), kFingerRequestIds.end()});
    REQUIRE(data.thumb.finger == Finger::kThumb);
    REQUIRE(data.pinky.finger == Finger::kPinky);
    REQUIRE(data.pinky.values[11][5] == 71);
    REQUIRE(hand.force_sensor.get_snapshot().has_value());
    REQUIRE(hand.get_snapshot().force_sensor.has_value());
}

// 状态字非零时抛 ProtocolError，并且 status() 要带回设备给的原因码。
TEST_CASE("comm: tactile non-zero status reports device status", "[comm][l30][force]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    install_tactile_handlers(raw, l30_comm::tactile_ramp(), 0x20);

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.force_sensor.get_finger(Finger::kThumb, kTimeout), ProtocolError);
    REQUIRE(thrown_status([&hand] { (void)hand.force_sensor.get_finger(Finger::kMiddle, kTimeout); }) == 0x20);
    // 校验顺序是「先状态再长度/事务」，所以报的是 0x20 而不是事务不匹配。
    REQUIRE(throws_with([&hand] { (void)hand.force_sensor.get_finger(Finger::kRing, kTimeout); },
                        "L30 tactile status 0x20"));
}

// 没有任何处理器时按超时报错，文本里要带上超时毫秒数。
TEST_CASE("comm: tactile timeout names the wait window", "[comm][l30][force]") {
    auto transport = std::make_unique<FakeTransport>();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE(throws_with([&hand] { (void)hand.force_sensor.get_finger(Finger::kThumb, kShortTimeout); },
                        "No L30 tactile response received within 50ms"));
    REQUIRE(throws_with([&hand] { (void)hand.force_sensor.get_finger(Finger::kThumb, kShortTimeout); }, "quarantined"));
}

// 两帧事务标记调包（先 0x11 再 0x10）：按 0x10 收到的帧长度只有 11，报长度不匹配。
TEST_CASE("comm: tactile swapped transaction markers throw", "[comm][l30][force]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(tactile_request_id(Finger::kThumb),
                    l30_comm::tactile_handler(l30_comm::tactile_ramp(), protocol::kStatusOk,
                                              protocol::kTactileSecondTransaction, protocol::kTactileFirstTransaction));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE(throws_with([&hand] { (void)hand.force_sensor.get_finger(Finger::kThumb, kTimeout); },
                        "tactile length mismatch: expected 11 bytes, got 61"));
    REQUIRE(throws_with([&hand] { (void)hand.force_sensor.get_finger(Finger::kThumb, kTimeout); }, "quarantined"));
}

// 事务标记既不是 0x10 也不是 0x11 时，Client 直接判整次读取失败。
TEST_CASE("comm: tactile unknown transaction marker throws", "[comm][l30][force]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(tactile_request_id(Finger::kThumb),
                    l30_comm::tactile_handler(l30_comm::tactile_ramp(), protocol::kStatusOk, 0x12, 0x13));

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE(throws_with([&hand] { (void)hand.force_sensor.get_finger(Finger::kThumb, kTimeout); },
                        "unexpected tactile transaction"));
    REQUIRE(throws_with([&hand] { (void)hand.force_sensor.get_finger(Finger::kThumb, kTimeout); }, "quarantined"));
}

// 非正超时先被参数校验挡住，一个字节都不许上总线。
TEST_CASE("comm: tactile rejects non-positive timeout", "[comm][l30][force]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    install_tactile_handlers(raw, l30_comm::tactile_ramp());

    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    raw->clear_sent();

    REQUIRE_THROWS_AS(hand.force_sensor.get_finger(Finger::kThumb, std::chrono::milliseconds{0}),
                      linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.force_sensor.get_blocking(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// 五个手指的名字与触觉子命令；越界枚举值报 unknown L30 finger。
TEST_CASE("comm: finger name and subcommand cover five fingers", "[comm][l30][force]") {
    REQUIRE(finger_name(Finger::kThumb) == "thumb");
    REQUIRE(finger_name(Finger::kIndex) == "index");
    REQUIRE(finger_name(Finger::kMiddle) == "middle");
    REQUIRE(finger_name(Finger::kRing) == "ring");
    REQUIRE(finger_name(Finger::kPinky) == "pinky");

    REQUIRE(finger_subcommand(Finger::kThumb) == protocol::kSubTactileThumb);
    REQUIRE(finger_subcommand(Finger::kIndex) == protocol::kSubTactileIndex);
    REQUIRE(finger_subcommand(Finger::kMiddle) == protocol::kSubTactileMiddle);
    REQUIRE(finger_subcommand(Finger::kRing) == protocol::kSubTactileRing);
    REQUIRE(finger_subcommand(Finger::kPinky) == protocol::kSubTactilePinky);

    // Fixed-underlying-type enum values are valid C++17, including unknown values and bit masks.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto bogus = static_cast<Finger>(9);
    REQUIRE_THROWS_AS(finger_name(bogus), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(finger_subcommand(bogus), linkerbot::hand::ValidationError);
    REQUIRE(throws_with([bogus] { (void)finger_name(bogus); }, "unknown L30 finger"));
    REQUIRE(throws_with([bogus] { (void)finger_subcommand(bogus); }, "unknown L30 finger"));
}

// from_payload 不碰总线也能验证行主序展开与手指标记。
TEST_CASE("comm: FingerForceData from_payload maps rows and columns", "[comm][l30][force]") {
    const auto matrix = l30_comm::tactile_ramp();
    const auto now = std::chrono::steady_clock::now();
    const auto data = FingerForceData::from_payload(Finger::kPinky, matrix, now);

    REQUIRE(data.finger == Finger::kPinky);
    REQUIRE(data.timestamp == now);
    REQUIRE(data.values[0][0] == 0);
    REQUIRE(data.values[0][5] == 5);
    REQUIRE(data.values[1][0] == 6);
    REQUIRE(data.values[11][5] == 71);
}

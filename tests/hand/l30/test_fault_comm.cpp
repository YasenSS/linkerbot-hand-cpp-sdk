#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/fault.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::kFaultCurrentBit;
using linkerbot::hand::l30::kFaultLoadBit;
using linkerbot::hand::l30::kFaultMagneticEncoderBit;
using linkerbot::hand::l30::kFaultTemperatureBit;
using linkerbot::hand::l30::kFaultVoltageBit;
using linkerbot::hand::l30::kJointCount;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::protocol::kEmptyRequestBytes;
using linkerbot::hand::l30::protocol::kSubFault;
using linkerbot::hand::l30::protocol::ProtocolError;

namespace {
    constexpr auto kTimeout = l30_comm::kTimeout;

    // 故障查询 0x5/0x05/R，应答是 17 字节 u8 向量（每关节一个状态字节）。
    constexpr uint32_t kQueryId = 0x00A0A100U;
    constexpr uint32_t kReportId = 0x0080A008U;

    // 头文件里定义了 BIT0 电压、BIT1 磁编码、BIT2 温度、BIT3 电流、BIT5 过载；
    // BIT4 与 BIT6/7 是保留位，没有对应访问器。
    constexpr uint8_t kVoltage = 1U << kFaultVoltageBit;         // 0x01
    constexpr uint8_t kEncoder = 1U << kFaultMagneticEncoderBit; // 0x02
    constexpr uint8_t kOvertemp = 1U << kFaultTemperatureBit;    // 0x04
    constexpr uint8_t kOvercurrent = 1U << kFaultCurrentBit;     // 0x08
    constexpr uint8_t kOverload = 1U << kFaultLoadBit;           // 0x20
    constexpr uint8_t kReserved = 0xD0U;                         // BIT4/6/7
    constexpr uint8_t kAllDefined = 0x2FU;                       // 0b00101111，五个已定义位全亮

    template <typename Ready> bool wait_until(Ready ready) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (ready()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return false;
    }

    auto read_faults(FakeTransport *transport, L30 &hand, const std::array<uint8_t, kJointCount> &values) {
        transport->on_request(kQueryId, l30_comm::u8_handler(values));
        return hand.fault.get_blocking(kTimeout);
    }
} // namespace

// FakeTransport 故障：只读，查询 0x00A0A100 空载荷，应答是 17 字节 u8 向量；
// 位置位表示该项异常，保留位没有访问器所以读不出任何故障。

// get_blocking 发空载荷查询帧（BYTE0=0、DLC=2）并刷新 snapshot。
TEST_CASE("comm: fault get_blocking sends empty query", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::u8_handler(l30_comm::filled_u8(0)));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_FALSE(hand.fault.get_snapshot().has_value());
    const auto data = hand.fault.get_blocking(kTimeout);

    const auto frames = l30_comm::frames_for(raw, kQueryId);
    REQUIRE(frames.size() == 1);
    REQUIRE(l30_comm::trimmed(frames.front()) == std::vector<uint8_t>{0x00, 0x00});
    REQUIRE(frames.front().dlc == kEmptyRequestBytes);
    REQUIRE(frames.front().is_extended);
    REQUIRE(frames.front().is_fd);
    REQUIRE(data.faults == l30_comm::filled_u8(0));
    REQUIRE(hand.fault.get_snapshot().has_value());
}

// BIT0 只点亮电压，其余四项都不该跟着亮。
TEST_CASE("comm: fault voltage bit stands alone", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = read_faults(raw, hand, l30_comm::filled_u8(kVoltage));
    REQUIRE(data.has_voltage_fault(0));
    REQUIRE_FALSE(data.has_magnetic_encoder_fault(0));
    REQUIRE_FALSE(data.has_temperature_fault(0));
    REQUIRE_FALSE(data.has_current_fault(0));
    REQUIRE_FALSE(data.has_load_fault(0));
    REQUIRE(data.has_any_fault(0));
}

// BIT1 只点亮磁编码器。
TEST_CASE("comm: fault magnetic encoder bit stands alone", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = read_faults(raw, hand, l30_comm::filled_u8(kEncoder));
    REQUIRE(data.has_magnetic_encoder_fault(3));
    REQUIRE_FALSE(data.has_voltage_fault(3));
    REQUIRE_FALSE(data.has_temperature_fault(3));
    REQUIRE_FALSE(data.has_current_fault(3));
    REQUIRE_FALSE(data.has_load_fault(3));
}

// BIT2 只点亮温度。
TEST_CASE("comm: fault temperature bit stands alone", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = read_faults(raw, hand, l30_comm::filled_u8(kOvertemp));
    REQUIRE(data.has_temperature_fault(8));
    REQUIRE_FALSE(data.has_voltage_fault(8));
    REQUIRE_FALSE(data.has_magnetic_encoder_fault(8));
    REQUIRE_FALSE(data.has_current_fault(8));
    REQUIRE_FALSE(data.has_load_fault(8));
}

// BIT3 只点亮电流。
TEST_CASE("comm: fault current bit stands alone", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = read_faults(raw, hand, l30_comm::filled_u8(kOvercurrent));
    REQUIRE(data.has_current_fault(16));
    REQUIRE_FALSE(data.has_voltage_fault(16));
    REQUIRE_FALSE(data.has_magnetic_encoder_fault(16));
    REQUIRE_FALSE(data.has_temperature_fault(16));
    REQUIRE_FALSE(data.has_load_fault(16));
}

// BIT5（不是 BIT4）只点亮过载。
TEST_CASE("comm: fault load bit stands alone", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = read_faults(raw, hand, l30_comm::filled_u8(kOverload));
    REQUIRE(kOverload == 0x20U);
    REQUIRE(data.has_load_fault(5));
    REQUIRE_FALSE(data.has_voltage_fault(5));
    REQUIRE_FALSE(data.has_magnetic_encoder_fault(5));
    REQUIRE_FALSE(data.has_temperature_fault(5));
    REQUIRE_FALSE(data.has_current_fault(5));
}

// 0b11010000 全是保留位（BIT4/6/7），没有访问器认它，所以读不出任何故障。
TEST_CASE("comm: fault reserved bits produce no flags", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = read_faults(raw, hand, l30_comm::filled_u8(kReserved));
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE_FALSE(data.has_voltage_fault(i));
        REQUIRE_FALSE(data.has_magnetic_encoder_fault(i));
        REQUIRE_FALSE(data.has_temperature_fault(i));
        REQUIRE_FALSE(data.has_current_fault(i));
        REQUIRE_FALSE(data.has_load_fault(i));
        REQUIRE_FALSE(data.has_any_fault(i));
    }
    // 原始字节仍原样保留，掩码只体现在访问器上。
    REQUIRE(data.faults[0] == kReserved);
}

// 0b00101111 = BIT0/1/2/3/5，五项已定义故障应同时亮。
TEST_CASE("comm: fault 0x2F lights all defined bits", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = read_faults(raw, hand, l30_comm::filled_u8(kAllDefined));
    REQUIRE(kAllDefined == static_cast<uint8_t>(kVoltage | kEncoder | kOvertemp | kOvercurrent | kOverload));
    REQUIRE(data.has_voltage_fault(0));
    REQUIRE(data.has_magnetic_encoder_fault(0));
    REQUIRE(data.has_temperature_fault(0));
    REQUIRE(data.has_current_fault(0));
    REQUIRE(data.has_load_fault(0));
    REQUIRE(data.has_any_fault(0));
}

// has_any_fault：0x00 为 false，任一已定义位为 true。
TEST_CASE("comm: fault has_any_fault tracks defined bits", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    auto values = l30_comm::filled_u8(0);
    values[0] = kVoltage;
    values[1] = kEncoder;
    values[2] = kOvertemp;
    values[3] = kOvercurrent;
    values[4] = kOverload;
    const auto data = read_faults(raw, hand, values);

    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(data.has_any_fault(i));
    }
    for (size_t i = 5; i < kJointCount; ++i) {
        REQUIRE_FALSE(data.has_any_fault(i));
    }
}

// 健康手：全零应答，17 个关节都读不出故障。
TEST_CASE("comm: fault all-zero read reports nothing", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = read_faults(raw, hand, l30_comm::filled_u8(0));
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(data.faults[i] == 0U);
        REQUIRE_FALSE(data.has_any_fault(i));
    }
}

// 故障周期上报（0x0080A008，无状态字节的 u8 向量）不发请求也应刷新 snapshot。
TEST_CASE("comm: fault periodic report updates snapshot", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto frame = l30_comm::periodic_u8_frame(kSubFault, l30_comm::filled_u8(kOverload));
    REQUIRE(frame.id == kReportId);
    raw->inject(frame);

    REQUIRE(wait_until([&hand] { return hand.fault.get_snapshot().has_value(); }));
    REQUIRE(hand.fault.get_snapshot()->has_load_fault(0));
    REQUIRE_FALSE(hand.fault.get_snapshot()->has_voltage_fault(0));
    REQUIRE(l30_comm::frames_for(raw, kQueryId).empty());
}

// timeout<=0 立刻抛 invalid_argument，不发帧。
TEST_CASE("comm: fault get rejects non-positive timeout", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::u8_handler(l30_comm::filled_u8(0)));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.fault.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.fault.get_blocking(std::chrono::milliseconds{-2}), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// 非零状态字节 → ProtocolError，status() 带回原因码。
TEST_CASE("comm: fault read surfaces device status", "[comm][l30][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::u8_handler(l30_comm::filled_u8(0), 0x23));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    uint8_t observed = 0xFFU;
    std::string message;
    try {
        (void)hand.fault.get_blocking(kTimeout);
    } catch (const ProtocolError &error) {
        observed = error.status();
        message = error.what();
    }
    REQUIRE(observed == 0x23U);
    REQUIRE(message.find("operation not allowed") != std::string::npos);
}

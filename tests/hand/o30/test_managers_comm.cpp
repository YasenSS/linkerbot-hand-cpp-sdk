#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <linkerbot/hand/o30/enable.hpp>
#include <linkerbot/hand/o30/fault.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o30/pid.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

#include "hand/o30/comm_helpers.hpp"

using linkerbot::hand::o30::kJointCount;
using linkerbot::hand::o30::PidTerm;
using linkerbot::hand::o30::protocol::kEnableOn;
using linkerbot::hand::o30::protocol::kMiAcceleration;
using linkerbot::hand::o30::protocol::kMiCurrent;
using linkerbot::hand::o30::protocol::kMiFaultV003;
using linkerbot::hand::o30::protocol::kMiPositionPidP;
using linkerbot::hand::o30::protocol::kMiSpeed;
using linkerbot::hand::o30::protocol::kMiStallHoldCurrent;
using linkerbot::hand::o30::protocol::kMiStallThreshold;
using linkerbot::hand::o30::protocol::kMiStallTime;
using linkerbot::hand::o30::protocol::kMiTemperature;
using linkerbot::hand::o30::protocol::kMiTorque;
using linkerbot::hand::o30::protocol::kMiVoltage;

namespace {
    constexpr auto kTimeout = o30_comm::kTimeout;

    std::vector<uint8_t> expect_full_read(uint8_t mi) { return {mi, 0x00, 0x24}; }
} // namespace

// FakeTransport 运行时对象：全量读投影 20 关节，全量写只碰装配槽。

// 速度 / 加速度 / 电流 / 电压 / 转矩 / 温度 / 故障：读 36B，返回物理关节。
TEST_CASE("comm: runtime managers read the full object and project joints", "[comm][o30][managers]") {
    auto h = o30_comm::make_hand();
    const auto ramp = o30_comm::ramp_slots();
    const auto expected = o30_comm::projected_ramp();
    h.hop->put(kMiSpeed, ramp);
    h.hop->put(kMiAcceleration, ramp);
    h.hop->put(kMiCurrent, ramp);
    h.hop->put(kMiVoltage, ramp);
    h.hop->put(kMiTorque, ramp);
    h.hop->put(kMiTemperature, ramp);
    h.hop->put(kMiFaultV003, ramp);

    REQUIRE(h.hand->speed.get_blocking(kTimeout).speeds == expected);
    REQUIRE(h.hand->acceleration.get_blocking(kTimeout).accelerations == expected);
    REQUIRE(h.hand->current.get_currents(kTimeout).currents == expected);
    REQUIRE(h.hand->voltage.get_blocking(kTimeout).voltages == expected);
    REQUIRE(h.hand->torque.get_torques(kTimeout).torques == expected);
    REQUIRE(h.hand->temperature.get_blocking(kTimeout).temperatures == expected);
    REQUIRE(h.hand->fault.get_blocking(kTimeout).faults == expected);

    const auto sent = o30_comm::payloads(h.transport);
    REQUIRE(sent[0] == expect_full_read(kMiSpeed));
    REQUIRE(sent[1] == expect_full_read(kMiAcceleration));
    REQUIRE(sent[2] == expect_full_read(kMiCurrent));
    REQUIRE(sent[3] == expect_full_read(kMiVoltage));
    REQUIRE(sent[4] == expect_full_read(kMiTorque));
    REQUIRE(sent[5] == expect_full_read(kMiTemperature));
    REQUIRE(sent[6] == std::vector<uint8_t>{0x41, 0x61, 8});
    REQUIRE(sent[7] == expect_full_read(kMiFaultV003));
}

TEST_CASE("comm: O30 actual values and limit setpoints use explicit APIs", "[comm][o30][managers]") {
    auto h = o30_comm::make_hand();
    const auto actual = o30_comm::ramp_slots();
    auto setpoint = actual;
    for (auto &value : setpoint) {
        value = static_cast<uint8_t>(value + 20);
    }
    const auto expected_actual = o30_comm::projected_ramp();
    auto expected_setpoint = expected_actual;
    for (auto &value : expected_setpoint) {
        value = static_cast<uint8_t>(value + 20);
    }
    h.hop->put(kMiCurrent, actual, false);
    h.hop->put(kMiCurrent, setpoint, true);
    h.hop->put(kMiTorque, actual, false);

    REQUIRE(h.hand->current.get_currents(kTimeout).currents == expected_actual);
    REQUIRE(h.hand->current.get_current_limits(kTimeout).current_limits == expected_setpoint);
    REQUIRE(h.hand->torque.get_torques(kTimeout).torques == expected_actual);
}

TEST_CASE("comm: O30 actual torque keeps valid snapshots on read failure", "[comm][o30][managers]") {
    auto h = o30_comm::make_hand();
    REQUIRE_FALSE(h.hand->torque.get_snapshot().has_value());
    REQUIRE_THROWS_AS(h.hand->torque.get_torques(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->torque.get_torques(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE(h.transport->sent().empty());

    h.hop->put(kMiTorque, o30_comm::ramp_slots());
    const auto data = h.hand->torque.get_torques(kTimeout);
    REQUIRE(data.torques == o30_comm::projected_ramp());
    REQUIRE(data.timestamp.time_since_epoch().count() > 0);
    REQUIRE(test_support::require_value(h.hand->torque.get_snapshot()).torques == data.torques);
    REQUIRE(test_support::require_value(h.hand->get_snapshot().torque).torques == data.torques);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x06, 0, 36});

    h.transport->on_request(1, [](const auto &) {
        return std::vector<linkerbot::can::Message>{
            o30_comm::hop_reply(0x401, 0x06, false, 0, std::vector<uint8_t>(35, 0))};
    });
    REQUIRE_THROWS_AS(h.hand->torque.get_torques(kTimeout), linkerbot::hand::o30::protocol::ProtocolError);
    REQUIRE(test_support::require_value(h.hand->torque.get_snapshot()).torques == data.torques);
    REQUIRE(test_support::require_value(h.hand->torque.get_snapshot()).timestamp == data.timestamp);
    REQUIRE(test_support::require_value(h.hand->get_snapshot().torque).torques == data.torques);
}

// set_all 类写入都走三段装配槽。
TEST_CASE("comm: full runtime writes touch only physical joint slots", "[comm][o30][managers]") {
    auto h = o30_comm::make_hand();
    h.hand->speed.set_all(10, kTimeout);
    h.hand->acceleration.set_all(20, kTimeout);
    h.hand->current.set_all_current_limits(30, kTimeout);

    REQUIRE(h.hop->writes.size() == 9);
    const std::array<uint8_t, 3> mis{kMiSpeed, kMiAcceleration, kMiCurrent};
    for (size_t group = 0; group < mis.size(); ++group) {
        REQUIRE(h.hop->writes[group * 3].mi == mis[group]);
        REQUIRE(h.hop->writes[group * 3].si == 0);
        REQUIRE(h.hop->writes[group * 3 + 1].si == 5);
        REQUIRE(h.hop->writes[group * 3 + 2].si == 16);
        REQUIRE(h.hop->writes[group * 3].payload.size() == 1);
        REQUIRE(h.hop->writes[group * 3 + 1].payload.size() == 10);
        REQUIRE(h.hop->writes[group * 3 + 2].payload.size() == 9);
    }
}

// 故障保留原始字节，非零即 has_any_fault；越界下标抛 out_of_range。
TEST_CASE("comm: fault data keeps raw bytes without inventing bit meanings", "[comm][o30][managers]") {
    auto h = o30_comm::make_hand();
    auto wire = o30_comm::ramp_slots();
    wire.assign(36, 0);
    wire[5] = 3;
    h.hop->put(kMiFaultV003, wire);

    const auto data = h.hand->fault.get_blocking(kTimeout);
    REQUIRE_FALSE(data.has_any_fault(0));
    REQUIRE(data.has_any_fault(1));
    REQUIRE(data.faults[1] == 3);
    REQUIRE_THROWS_AS(data.has_any_fault(kJointCount), std::out_of_range);
}

// PID 按 36 字节对象读写（固件不是文档里的 72B）。
TEST_CASE("comm: PID terms are 36-byte objects", "[comm][o30][managers]") {
    auto h = o30_comm::make_hand();
    h.hop->put(kMiPositionPidP, o30_comm::ramp_slots());

    const auto data = h.hand->pid.get(PidTerm::kPositionP, kTimeout);
    REQUIRE(data.values == o30_comm::projected_ramp());
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>({kMiPositionPidP, 0x00, 0x24}));

    h.transport->clear_sent();
    h.hand->pid.set(PidTerm::kPositionP, o30_comm::filled_u8(7), kTimeout);
    REQUIRE(o30_comm::write_segments(h.hop, kMiPositionPidP).size() == 3);
}

// 堵转时间 / 阈值 / 维持电流三个 MI。
TEST_CASE("comm: stall managers use 0x09 0x0A 0x0B", "[comm][o30][managers]") {
    auto h = o30_comm::make_hand();
    h.hop->put(kMiStallTime, o30_comm::ramp_slots());
    h.hop->put(kMiStallThreshold, o30_comm::ramp_slots());
    h.hop->put(kMiStallHoldCurrent, o30_comm::ramp_slots());

    REQUIRE(h.hand->stall.get_times(kTimeout).times == o30_comm::projected_ramp());
    REQUIRE(h.hand->stall.get_thresholds(kTimeout).thresholds == o30_comm::projected_ramp());
    REQUIRE(h.hand->stall.get_hold_currents(kTimeout).currents == o30_comm::projected_ramp());

    h.hand->stall.set_times(o30_comm::filled_u8(3), kTimeout);
    h.hand->stall.set_thresholds(o30_comm::filled_u8(4), kTimeout);
    h.hand->stall.set_hold_currents(o30_comm::filled_u8(5), kTimeout);
    REQUIRE(o30_comm::write_segments(h.hop, kMiStallTime).size() == 3);
    REQUIRE(o30_comm::write_segments(h.hop, kMiStallThreshold).size() == 3);
    REQUIRE(o30_comm::write_segments(h.hop, kMiStallHoldCurrent).size() == 3);
}

// 旧使能接口必须拒绝操作，不能误读写故障对象。
TEST_CASE("comm: unsupported enable never sends a frame", "[comm][o30][managers]") {
    auto h = o30_comm::make_hand();
    REQUIRE_THROWS_AS(h.hand->enable.get_blocking(kTimeout), linkerbot::hand::UnsupportedOperationError);
    REQUIRE_THROWS_AS(h.hand->enable.set_masks(o30_comm::filled_u8(kEnableOn), kTimeout),
                      linkerbot::hand::UnsupportedOperationError);
    REQUIRE_FALSE(h.hand->enable.get_snapshot().has_value());
    REQUIRE_THROWS_AS(h.hand->fault.set_faults(o30_comm::filled_u8(0), kTimeout),
                      linkerbot::hand::UnsupportedOperationError);
    REQUIRE(h.transport->sent().empty());
}

// 用字面线值校验 MI 和 RTS，避免常量与测试一起写错。
TEST_CASE("comm: O30 0.0.4 fault MI and current write RTS", "[comm][o30][managers]") {
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(0x41, 0x61, 8, "0.0.4");
    (void)h.hand->fault.get_blocking(kTimeout);
    REQUIRE(h.transport->sent().back().data[0] == 0x0C);
    h.transport->clear_sent();
    h.hand->current.set_all_current_limits(42, kTimeout);
    REQUIRE(h.transport->sent().size() == 3);
    for (const auto &frame : h.transport->sent()) {
        REQUIRE(frame.data[0] == 0x84);
    }
}

#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20lite/fault.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l20lite::Acceleration;
using linkerbot::hand::l20lite::Angle;
using linkerbot::hand::l20lite::Fault;
using linkerbot::hand::l20lite::FaultCode;
using linkerbot::hand::l20lite::has_fault;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::Side;
using linkerbot::hand::l20lite::Speed;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{100};

    std::array<float, Angle::kJointCount> filled(float v) {
        std::array<float, Angle::kJointCount> values{};
        values.fill(v);
        return values;
    }
} // namespace

// 角度构造/from_array 拒绝越界与 NaN，接受 [0, 100]；to_array 往返；[] 越界抛。
TEST_CASE("comm: Angle rejects out-of-range and accepts bounds", "[comm][l20lite][validation]") {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS_AS(Angle(-1, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Angle(101, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Angle(nan, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);

    REQUIRE_NOTHROW(Angle(0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
    REQUIRE_NOTHROW(Angle(100, 100, 100, 100, 100, 100, 100, 100, 100, 100));

    REQUIRE_THROWS_AS(Angle::from_array(filled(-1)), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Angle::from_array(filled(101)), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Angle::from_array(filled(nan)), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(Angle::from_array(filled(0)));
    REQUIRE_NOTHROW(Angle::from_array(filled(100)));

    const Angle src(10, 20, 30, 40, 50, 60, 70, 80, 90, 100);
    REQUIRE(Angle::from_array(src.to_array()).to_array() == src.to_array());

    REQUIRE(Angle(50, 0, 0, 0, 0, 0, 0, 0, 0, 0)[0] == 50.0f);
    REQUIRE_THROWS_AS(Angle(50, 0, 0, 0, 0, 0, 0, 0, 0, 0)[10], std::out_of_range);
}

// Speed 构造越界抛；Acceleration 用 from_array 全 101，避免与 acceleration_comm 重复。
TEST_CASE("comm: Speed Acceleration reject out-of-range", "[comm][l20lite][validation]") {
    REQUIRE_THROWS_AS(Speed(101, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Speed(-1, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Acceleration::from_array(filled(101)), linkerbot::hand::ValidationError);
}

// 全 None 无故障；组合后 has_any_fault；has_fault / | / &；to_array 越界。
TEST_CASE("comm: Fault has_any_fault and indexing", "[comm][l20lite][validation]") {
    const Fault none;
    REQUIRE_FALSE(none.has_any_fault());
    REQUIRE_FALSE(Fault::from_raw({0, 0, 0, 0, 0, 0, 0, 0, 0, 0}).has_any_fault());

    Fault combined;
    combined.thumb_flex = FaultCode::kVoltage;
    REQUIRE(combined.has_any_fault());

    REQUIRE_FALSE(has_fault(FaultCode::kNone));
    REQUIRE(has_fault(FaultCode::kOvercurrent));

    const auto mixed = FaultCode::kVoltage | FaultCode::kOvercurrent;
    REQUIRE(has_fault(mixed));
    REQUIRE((mixed & FaultCode::kVoltage) == FaultCode::kVoltage);
    REQUIRE((mixed & FaultCode::kOvertemp) == FaultCode::kNone);

    REQUIRE_THROWS_AS(Fault().to_array().at(10), std::out_of_range);
}

// timeout<=0 应立刻抛 invalid_argument；同手测 angle/speed/current/temperature。
TEST_CASE("comm: get_blocking rejects non-positive timeout", "[comm][l20lite][validation]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId,
                          l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x01, 6, 0x04, 4, 0));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.angle.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.angle.get_blocking(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.current.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.temperature.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

// 未知手指名：力传感器与指尖力都应抛 out_of_range。
TEST_CASE("comm: unknown finger name is rejected", "[comm][l20lite][validation]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId,
                          l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x01, 6, 0x04, 4, 0));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.force_sensor.get_finger("nope"), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.tip_force.get_finger_blocking("nope", kTimeout), linkerbot::hand::ValidationError);
}

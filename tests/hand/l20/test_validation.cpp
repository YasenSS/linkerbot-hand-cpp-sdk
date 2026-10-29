#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20/fault.hpp>
#include <linkerbot/hand/l20/l20.hpp>

#include "hand/l20/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l20::Angle;
using linkerbot::hand::l20::Fault;
using linkerbot::hand::l20::has_fault;
using linkerbot::hand::l20::L20;
using linkerbot::hand::l20::L20FaultCode;
using linkerbot::hand::l20::Side;
using linkerbot::hand::l20::Speed;
using linkerbot::hand::l20::Temperature;

namespace {
    std::array<float, 16> filled(float v) {
        std::array<float, 16> values{};
        values.fill(v);
        return values;
    }
} // namespace

// 角度构造/from_array 拒绝越界与 NaN，接受 [0, 100]；to_array 往返；[] 越界抛。
TEST_CASE("comm: Angle rejects out-of-range and accepts bounds", "[comm][l20][validation]") {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS_AS(Angle(-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Angle(101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Angle(nan, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);

    REQUIRE_NOTHROW(Angle(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
    REQUIRE_NOTHROW(Angle(100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100));

    REQUIRE_THROWS_AS(Angle::from_array(filled(-1)), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Angle::from_array(filled(101)), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Angle::from_array(filled(nan)), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(Angle::from_array(filled(0)));
    REQUIRE_NOTHROW(Angle::from_array(filled(100)));

    const Angle src(10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 15, 25, 35, 45, 55, 65);
    REQUIRE(Angle::from_array(src.to_array()).to_array() == src.to_array());

    REQUIRE(Angle(50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)[0] == 50.0f);
    REQUIRE_THROWS_AS(Angle(50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)[16], std::out_of_range);
}

// Speed 构造与 from_array 拒绝越界与 NaN，接受 [0, 100]；往返与 [] 越界。
TEST_CASE("comm: Speed rejects out-of-range and accepts bounds", "[comm][l20][validation]") {
    const auto nan = std::numeric_limits<float>::quiet_NaN();

    REQUIRE_THROWS_AS(Speed(-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Speed(101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Speed(nan, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(Speed(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
    REQUIRE_NOTHROW(Speed(100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100));
    REQUIRE_THROWS_AS(Speed::from_array(filled(-1)), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Speed::from_array(filled(101)), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Speed::from_array(filled(nan)), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(Speed::from_array(filled(0)));
    REQUIRE_NOTHROW(Speed::from_array(filled(100)));

    const Speed speed_src(10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 15, 25, 35, 45, 55, 65);
    REQUIRE(Speed::from_array(speed_src.to_array()).to_array() == speed_src.to_array());
    REQUIRE(Speed(50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)[0] == 50.0f);
    REQUIRE_THROWS_AS(Speed(50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)[16], std::out_of_range);
}

// Temperature 无 validate()，不按 0–100 拒绝；[] 越界仍抛。
TEST_CASE("comm: Temperature does not validate range", "[comm][l20][validation]") {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_NOTHROW(Temperature(-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
    REQUIRE_NOTHROW(Temperature(101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
    REQUIRE_NOTHROW(Temperature::from_array(filled(-1)));
    REQUIRE_NOTHROW(Temperature::from_array(filled(101)));
    REQUIRE_NOTHROW(Temperature::from_array(filled(nan)));

    const Temperature src(10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 15, 25, 35, 45, 55, 65);
    REQUIRE(Temperature::from_array(src.to_array()).to_array() == src.to_array());
    REQUIRE(Temperature(50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)[0] == 50.0f);
    REQUIRE_THROWS_AS(Temperature(50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)[16], std::out_of_range);
}

// 默认/全零无故障；组合后 has_any_fault；has_fault / | / &；to_array 越界。
TEST_CASE("comm: Fault has_any_fault and indexing", "[comm][l20][validation]") {
    const Fault none;
    REQUIRE_FALSE(none.has_any_fault());
    REQUIRE_FALSE(Fault::from_raw({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}).has_any_fault());

    Fault combined;
    combined.thumb_abd = L20FaultCode::kVoltageAbnormal;
    REQUIRE(combined.has_any_fault());

    REQUIRE_FALSE(has_fault(L20FaultCode::kNone));
    REQUIRE(has_fault(L20FaultCode::kMotorOverCurrent));

    const auto mixed = L20FaultCode::kVoltageAbnormal | L20FaultCode::kMotorOverCurrent;
    REQUIRE(has_fault(mixed));
    REQUIRE((mixed & L20FaultCode::kVoltageAbnormal) == L20FaultCode::kVoltageAbnormal);
    REQUIRE((mixed & L20FaultCode::kOverTemperature) == L20FaultCode::kNone);

    REQUIRE_THROWS_AS(Fault().to_array().at(16), std::out_of_range);
}

// timeout<=0 应立刻抛 invalid_argument；同手测 angle/speed/current/temperature/fault/force_sensor。
TEST_CASE("comm: get_blocking rejects non-positive timeout", "[comm][l20][validation]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, 0x41, 0));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.angle.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.angle.get_blocking(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.get_blocking(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.current.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.current.get_blocking(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.temperature.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.temperature.get_blocking(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.fault.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.fault.get_blocking(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.force_sensor.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.force_sensor.get_blocking(std::chrono::milliseconds{-1}), linkerbot::hand::ValidationError);
}

// 未知手指名：力传感器应抛 out_of_range。
TEST_CASE("comm: unknown finger name is rejected", "[comm][l20][validation]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, 0x41, 0));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.force_sensor.get_finger("nope"), linkerbot::hand::ValidationError);
}

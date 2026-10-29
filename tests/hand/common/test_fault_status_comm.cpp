#include <catch2/catch_test_macros.hpp>

#include <linkerbot/hand/l20/fault.hpp>
#include <linkerbot/hand/l20lite/fault.hpp>
#include <linkerbot/hand/l30/fault.hpp>
#include <linkerbot/hand/l6/fault.hpp>
#include <linkerbot/hand/o30/fault.hpp>
#include <linkerbot/hand/o6/fault.hpp>

namespace {
    template <typename Fault, typename Data> void check_classic(uint8_t mask) {
        for (unsigned value = 0; value <= 255; ++value) {
            std::array<uint8_t, Fault::kJointCount> raw{};
            raw.fill(static_cast<uint8_t>(value));
            const auto fault = Fault::from_raw(raw);
            const Data data{fault, {}};
            REQUIRE(fault.to_raw() == raw);
            const auto status = data.status(0);
            REQUIRE(status.raw == value);
            REQUIRE(status.known_bits == (value & mask));
            REQUIRE(status.reserved_bits == (value & (255U ^ mask)));
            REQUIRE(status.has_any_fault() == ((value & mask) != 0));
            REQUIRE(fault.has_any_fault() == status.has_any_fault());
            REQUIRE(status.has_any_bit() == (value != 0));
        }
    }
}

// 穷举 256 个字节，所有经典手型保留原始位，已定义位判定与保留位分离。
TEST_CASE("comm: fault decoding preserves all bytes across models", "[comm][fault][decode]") {
    using namespace linkerbot::hand;
    check_classic<l6::Fault, l6::FaultData>(0x7F);
    check_classic<o6::Fault, o6::FaultData>(0x2F);
    check_classic<l20::Fault, l20::FaultData>(0xFF);
    check_classic<l20lite::Fault, l20lite::FaultData>(0x2F);
    for (unsigned value = 0; value <= 255; ++value) {
        l30::FaultData l30_data{};
        o30::FaultData o30_data{};
        l30_data.faults.fill(static_cast<uint8_t>(value));
        o30_data.faults.fill(static_cast<uint8_t>(value));
        REQUIRE(l30_data.status(0).raw == value);
        REQUIRE(l30_data.status(0).known_bits == (value & 0x2F));
        REQUIRE(l30_data.status(0).reserved_bits == (value & 0xD0));
        REQUIRE(l30_data.has_any_fault(0) == ((value & 0x2F) != 0));
        REQUIRE(o30_data.status(0).raw == value);
        REQUIRE(o30_data.status(0).known_bits == (value & 0xCF));
        REQUIRE(o30_data.status(0).reserved_bits == (value & 0x30));
        REQUIRE(o30_data.has_any_fault(0) == ((value & 0xCF) != 0));
        REQUIRE(o30_data.has_fault(0, o30::FaultCode::kHandLayerStall) == ((value & 1) != 0));
        REQUIRE(o30_data.has_fault(0, o30::FaultCode::kActuatorLayerStall) == ((value & 2) != 0));
        REQUIRE(o30_data.has_fault(0, o30::FaultCode::kOverCurrent) == ((value & 4) != 0));
        REQUIRE(o30_data.has_fault(0, o30::FaultCode::kOverTemperature) == ((value & 8) != 0));
        REQUIRE(o30_data.has_fault(0, o30::FaultCode::kActuatorAbnormal) == ((value & 0x40) != 0));
        REQUIRE(o30_data.has_fault(0, o30::FaultCode::kActuatorOffline) == ((value & 0x80) != 0));
    }
}

// 跨型号访问器对错误关节索引一致抛出 out_of_range。
TEST_CASE("comm: fault status checks joint bounds", "[comm][fault][decode]") {
    using namespace linkerbot::hand;
    REQUIRE_THROWS_AS(l6::FaultData{}.status(6), std::out_of_range);
    REQUIRE_THROWS_AS(o6::FaultData{}.status(6), std::out_of_range);
    REQUIRE_THROWS_AS(l20::FaultData{}.status(16), std::out_of_range);
    REQUIRE_THROWS_AS(l20lite::FaultData{}.status(10), std::out_of_range);
    REQUIRE_THROWS_AS(l30::FaultData{}.status(l30::kJointCount), std::out_of_range);
    REQUIRE_THROWS_AS(o30::FaultData{}.status(o30::kJointCount), std::out_of_range);
}

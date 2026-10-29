#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstddef>
#include <stdexcept>

#include "hand/l30/l30_fixture.hpp"

using namespace linkerbot::hand::l30;

// 真机故障位。查询 0x5/0x05，u8 向量，每关节一个状态字节，置位即异常；
// 一只健康的手所有关节都不该报故障。

// 健康的手：17 个关节都不该有任何故障位。
TEST_CASE("Fault: healthy hand reports no faults", "[l30][sensor][fault]") {
    auto &hand = test_util::l30_hand();
    const auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE_FALSE(data.has_any_fault(i));
    }
}

// 逐项确认五个已定义位都没亮，故障时便于定位是哪一类。
TEST_CASE("Fault: no individual condition is flagged", "[l30][sensor][fault]") {
    auto &hand = test_util::l30_hand();
    const auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE_FALSE(data.has_voltage_fault(i));
        REQUIRE_FALSE(data.has_magnetic_encoder_fault(i));
        REQUIRE_FALSE(data.has_temperature_fault(i));
        REQUIRE_FALSE(data.has_current_fault(i));
        REQUIRE_FALSE(data.has_load_fault(i));
    }
}

// timeout<=0 该立刻抛，不落到总线上。
TEST_CASE("Fault: rejects non-positive timeout", "[l30][sensor][fault]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_THROWS_AS(hand.fault.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

// get_blocking 之后，整机快照里 fault 应有值。
TEST_CASE("Fault: snapshot populated after read", "[l30][sensor][fault]") {
    auto &hand = test_util::l30_hand();
    (void)hand.fault.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().fault.has_value());
}

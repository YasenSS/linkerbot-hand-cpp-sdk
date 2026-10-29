#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstddef>
#include <stdexcept>

#include "hand/l30/l30_fixture.hpp"

using namespace linkerbot::hand::l30;

// 真机温度。查询 0x5/0x04，u8 向量，1 ℃ 每计数；协议未给上限，
// 这里按「常温环境下的舵机」拦异常值。
static constexpr int kAmbientMin = 5;
static constexpr int kAmbientMax = 90;

// 读回 17 个温度，都应落在常温环境下的合理区间 [5, 90] ℃。
TEST_CASE("Temperature: get_blocking returns ambient range", "[l30][sensor][temperature]") {
    auto &hand = test_util::l30_hand();
    const auto data = hand.temperature.get_blocking(std::chrono::milliseconds{500});
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(static_cast<int>(data.temperatures[i]) >= kAmbientMin);
        REQUIRE(static_cast<int>(data.temperatures[i]) <= kAmbientMax);
    }
}

// 应答该带满 17 个关节的温度。
TEST_CASE("Temperature: reports all seventeen joints", "[l30][sensor][temperature]") {
    auto &hand = test_util::l30_hand();
    const auto data = hand.temperature.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(data.temperatures.size() == kJointCount);
}

// timeout<=0 该立刻抛，不落到总线上。
TEST_CASE("Temperature: rejects non-positive timeout", "[l30][sensor][temperature]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_THROWS_AS(hand.temperature.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

// get_blocking 之后，整机快照里 temperature 应有值。
TEST_CASE("Temperature: snapshot populated after read", "[l30][sensor][temperature]") {
    auto &hand = test_util::l30_hand();
    (void)hand.temperature.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().temperature.has_value());
}

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>

#include "hand/o30/o30_fixture.hpp"

using namespace linkerbot::hand::o30;

// 真机温度。MI 0x07，20 路原始计数；按 [0, 150] 拦异常值。

// 20 个温度计数都应 ≤150。
TEST_CASE("Temperature: get_blocking returns plausible range", "[o30][sensor][temperature]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.temperature.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(data.temperatures.size() == kJointCount);
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(data.temperatures[i] <= 150);
    }
}

// get_blocking 之后整机快照里 temperature 应有值。
TEST_CASE("Temperature: snapshot populated after read", "[o30][sensor][temperature]") {
    auto &hand = test_util::o30_hand();
    static_cast<void>(hand.temperature.get_blocking(std::chrono::milliseconds{500}));
    REQUIRE(hand.get_snapshot().temperature.has_value());
}

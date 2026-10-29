#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <stdexcept>

#include "hand/l30/l30_fixture.hpp"

using namespace linkerbot::hand::l30;

// 真机电流。查询 0x5/0x02（与扭矩写共用子命令 0x02），有符号计数，
// 6.5 mA 每计数；空载静止时电流应该很小。
static constexpr float kIdleMilliampsLimit = 3000.0f;

// 读回 17 个有符号计数，空载下换算成毫安应都在 3 A 以内。
TEST_CASE("Current: get_blocking returns idle magnitudes", "[l30][sensor][current]") {
    auto &hand = test_util::l30_hand();
    const auto data = hand.current.get_blocking(std::chrono::milliseconds{500});
    for (size_t i = 0; i < kJointCount; ++i) {
        const auto milliamps = std::abs(static_cast<float>(data.currents[i])) * kCurrentMilliampsPerCount;
        REQUIRE(milliamps <= kIdleMilliampsLimit);
    }
}

// timeout<=0 该立刻抛，不落到总线上。
TEST_CASE("Current: rejects non-positive timeout", "[l30][sensor][current]") {
    auto &hand = test_util::l30_hand();
    REQUIRE_THROWS_AS(hand.current.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

// get_blocking 之后，整机快照里 current 应有值。
TEST_CASE("Current: snapshot populated after read", "[l30][sensor][current]") {
    auto &hand = test_util::l30_hand();
    (void)hand.current.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().current.has_value());
}

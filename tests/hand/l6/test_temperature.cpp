#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include "hand/l6/l6_fixture.hpp"

using namespace linkerbot::hand::l6;

// 真机温度。0x33 读六关节，单位 ℃；协议未给上限，这里按 [0, 150] 拦异常值。

// get_blocking 六个温度都应在 [0, 150]。
TEST_CASE("Temperature: get_blocking returns valid range", "[l6][sensor][temperature]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.temperature.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.temperature.to_array();
    for (size_t i = 0; i < Temperature::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
        REQUIRE(arr[i] <= 150.0f);
    }
}

// 按字段名也能读到非负温度。
TEST_CASE("Temperature: field access by name", "[l6][sensor][temperature]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.temperature.get_blocking(std::chrono::milliseconds{500});
    auto &t = data.temperature;

    REQUIRE(t.thumb_flex >= 0.0f);
    REQUIRE(t.thumb_abd >= 0.0f);
    REQUIRE(t.index >= 0.0f);
    REQUIRE(t.middle >= 0.0f);
    REQUIRE(t.ring >= 0.0f);
    REQUIRE(t.pinky >= 0.0f);
}

// get_blocking 之后，整机快照里 temperature 应有值。
TEST_CASE("Temperature: snapshot populated after read", "[l6][sensor][temperature]") {
    auto &hand = test_util::l6_hand();
    (void)hand.temperature.get_blocking(std::chrono::milliseconds{500});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.temperature.has_value());
}

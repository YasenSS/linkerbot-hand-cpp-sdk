#include <catch2/catch_test_macros.hpp>

#include "hand/l20lite/l20lite_fixture.hpp"

using namespace linkerbot::hand::l20lite;

// 真机温度。0x33+0x34，单位 ℃；协议未给上限，这里按 [0, 150] 拦异常值。

// get_blocking 十个温度都应在 [0, 150]。
TEST_CASE("Temperature: get_blocking returns valid range", "[l20lite][sensor][temperature]") {
    auto &hand = test_util::l20lite_hand();
    auto arr = hand.temperature.get_blocking(std::chrono::milliseconds{500}).temperature.to_array();
    for (auto v : arr) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 150.0f);
    }
}

// get_blocking 之后，整机快照里 temperature 应有值。
TEST_CASE("Temperature: snapshot populated after read", "[l20lite][sensor][temperature]") {
    auto &hand = test_util::l20lite_hand();
    (void)hand.temperature.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().temperature.has_value());
}

// 按字段名也能读到非负温度。
TEST_CASE("Temperature: field access by name", "[l20lite][sensor][temperature]") {
    auto &hand = test_util::l20lite_hand();
    auto data = hand.temperature.get_blocking(std::chrono::milliseconds{500});
    auto &t = data.temperature;

    REQUIRE(t.thumb_flex >= 0.0f);
    REQUIRE(t.thumb_abd >= 0.0f);
    REQUIRE(t.index_flex >= 0.0f);
    REQUIRE(t.middle_flex >= 0.0f);
    REQUIRE(t.ring_flex >= 0.0f);
    REQUIRE(t.pinky_flex >= 0.0f);
    REQUIRE(t.index_abd >= 0.0f);
    REQUIRE(t.ring_abd >= 0.0f);
    REQUIRE(t.pinky_abd >= 0.0f);
    REQUIRE(t.thumb_yaw >= 0.0f);
}

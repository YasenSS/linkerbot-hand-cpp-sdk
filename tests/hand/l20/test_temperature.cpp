#include <catch2/catch_test_macros.hpp>

#include <thread>

#include "hand/l20/l20_fixture.hpp"

using namespace linkerbot::hand::l20;

// 真机温度。0x61–0x65 共十六路电机，单位 ℃；协议未给上限，这里按 [0, 150] 拦异常值。

// get_blocking 十六个温度都应在 [0, 150]。
TEST_CASE("Temperature: get_blocking returns valid range", "[l20][sensor][temperature]") {
    auto &hand = test_util::l20_hand();
    auto arr = hand.temperature.get_blocking(std::chrono::milliseconds{500}).temperature.to_array();
    REQUIRE(arr.size() == Temperature::kJointCount);
    for (auto v : arr) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 150.0f);
    }
}

// get_blocking 之后，整机快照里 temperature 应有值。
TEST_CASE("Temperature: snapshot populated after read", "[l20][sensor][temperature]") {
    auto &hand = test_util::l20_hand();
    (void)hand.temperature.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(hand.get_snapshot().temperature.has_value());
}

// 按十六个关节字段名也能读到非负温度。
TEST_CASE("Temperature: field access by name", "[l20][sensor][temperature]") {
    auto &hand = test_util::l20_hand();
    auto data = hand.temperature.get_blocking(std::chrono::milliseconds{500});
    auto &t = data.temperature;

    REQUIRE(t.thumb_abd >= 0.0f);
    REQUIRE(t.thumb_yaw >= 0.0f);
    REQUIRE(t.thumb_root1 >= 0.0f);
    REQUIRE(t.thumb_tip >= 0.0f);
    REQUIRE(t.index_abd >= 0.0f);
    REQUIRE(t.index_root1 >= 0.0f);
    REQUIRE(t.index_tip >= 0.0f);
    REQUIRE(t.middle_abd >= 0.0f);
    REQUIRE(t.middle_root1 >= 0.0f);
    REQUIRE(t.middle_tip >= 0.0f);
    REQUIRE(t.ring_abd >= 0.0f);
    REQUIRE(t.ring_root1 >= 0.0f);
    REQUIRE(t.ring_tip >= 0.0f);
    REQUIRE(t.pinky_abd >= 0.0f);
    REQUIRE(t.pinky_root1 >= 0.0f);
    REQUIRE(t.pinky_tip >= 0.0f);
}

// 两次阻塞读取，第二次 timestamp 应更晚。
TEST_CASE("Temperature: timestamp advances between reads", "[l20][sensor][temperature]") {
    auto &hand = test_util::l20_hand();
    auto first = hand.temperature.get_blocking(std::chrono::milliseconds{500});
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    auto second = hand.temperature.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(second.timestamp > first.timestamp);
}

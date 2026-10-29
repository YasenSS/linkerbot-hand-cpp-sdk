#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include "hand/l6/l6_fixture.hpp"

using namespace linkerbot::hand::l6;

TEST_CASE("Current: get_blocking returns non-negative", "[l6][sensor][current]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.current.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.current.to_array();
    for (size_t i = 0; i < Current::kJointCount; ++i) {
        REQUIRE(arr[i] >= 0.0f);
    }
}

TEST_CASE("Current: field access by name", "[l6][sensor][current]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.current.get_blocking(std::chrono::milliseconds{500});
    auto &c = data.current;

    REQUIRE(c.thumb_flex >= 0.0f);
    REQUIRE(c.thumb_abd >= 0.0f);
    REQUIRE(c.index >= 0.0f);
    REQUIRE(c.middle >= 0.0f);
    REQUIRE(c.ring >= 0.0f);
    REQUIRE(c.pinky >= 0.0f);
}

TEST_CASE("Current: snapshot populated after read", "[l6][sensor][current]") {
    auto &hand = test_util::l6_hand();
    (void)hand.current.get_blocking(std::chrono::milliseconds{500});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.current.has_value());
}

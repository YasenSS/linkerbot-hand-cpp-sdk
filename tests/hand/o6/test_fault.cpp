#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include <linkerbot/hand/o6/fault.hpp>

#include "hand/o6/o6_fixture.hpp"

using namespace linkerbot::hand::o6;

// 真机故障码。0x35 读六关节错误码；新协议无 0x83 清故障。
// 健康手应为 kNone（0）。

// get_blocking 能读回六关节错误码数组。
TEST_CASE("Fault: get_blocking returns valid data", "[o6][sensor][fault]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.fault.to_array();
    REQUIRE(arr.size() == Fault::kJointCount);
}

// 健康手六个关节都是 kNone。
TEST_CASE("Fault: no fault on healthy device", "[o6][sensor][fault]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.fault.to_array();
    for (size_t i = 0; i < Fault::kJointCount; ++i) {
        REQUIRE(arr[i] == FaultCode::kNone);
    }
}

// 健康手 has_any_fault() 为 false。
TEST_CASE("Fault: has_any_fault false when healthy", "[o6][sensor][fault]") {
    auto &hand = test_util::o6_hand();
    auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    REQUIRE_FALSE(data.fault.has_any_fault());
}

// get_blocking 之后 snapshot.fault 应有值。
TEST_CASE("Fault: snapshot populated after read", "[o6][sensor][fault]") {
    auto &hand = test_util::o6_hand();
    (void)hand.fault.get_blocking(std::chrono::milliseconds{500});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.fault.has_value());
}

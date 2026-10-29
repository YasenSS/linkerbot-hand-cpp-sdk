#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include <linkerbot/hand/l6/fault.hpp>

#include "hand/l6/l6_fixture.hpp"

using namespace linkerbot::hand::l6;

// 真机故障码。0x35 读六关节错误码，0x83 清故障；顺序：拇指屈曲、外展、食、中、无名、小指。
// 健康手应为 kNone（0）。过流/过载/过温是按位置 1，本文件不人为制造故障。

// get_blocking 能读回 6 个关节的故障码（只检查长度，不看具体值）。
TEST_CASE("Fault: get_blocking returns valid data", "[l6][sensor][fault]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.fault.to_array();
    REQUIRE(arr.size() == Fault::kJointCount);
}

// 健康设备上，六个关节都应是 kNone。
TEST_CASE("Fault: no fault on healthy device", "[l6][sensor][fault]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    auto arr = data.fault.to_array();
    for (size_t i = 0; i < Fault::kJointCount; ++i) {
        REQUIRE(arr[i] == FaultCode::kNone);
    }
}

// has_any_fault()：六关节全无故障时应为 false。
TEST_CASE("Fault: has_any_fault false when healthy", "[l6][sensor][fault]") {
    auto &hand = test_util::l6_hand();
    auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    REQUIRE_FALSE(data.fault.has_any_fault());
}

// clear_faults() 发 0x83 清全部关节，不应抛错（协议无回包，只检查发送成功）。
TEST_CASE("Fault: clear_faults no error", "[l6][sensor][fault]") {
    auto &hand = test_util::l6_hand();
    REQUIRE_NOTHROW(hand.fault.clear_faults());
}

// get_blocking 之后，整机快照里 fault 应有值。
TEST_CASE("Fault: snapshot populated after read", "[l6][sensor][fault]") {
    auto &hand = test_util::l6_hand();
    (void)hand.fault.get_blocking(std::chrono::milliseconds{500});
    auto snap = hand.get_snapshot();
    REQUIRE(snap.fault.has_value());
}

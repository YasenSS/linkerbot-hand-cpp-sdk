#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>

#include "hand/o30/o30_fixture.hpp"

using namespace linkerbot::hand::o30;

// 真机故障：0.0.3 读 MI 0x0D，0.0.4 读 MI 0x0C；保留原始字节。

// 读回应有 20 路故障字节。
TEST_CASE("Fault: get_blocking returns 20 values", "[o30][sensor][fault]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    REQUIRE(data.faults.size() == kJointCount);
}

// get_blocking 之后整机快照里 fault 应有值。
TEST_CASE("Fault: snapshot populated after read", "[o30][sensor][fault]") {
    auto &hand = test_util::o30_hand();
    static_cast<void>(hand.fault.get_blocking(std::chrono::milliseconds{500}));
    REQUIRE(hand.get_snapshot().fault.has_value());
}

// 每个关节都能问 has_any_fault，越界由 array::at 抛。
TEST_CASE("Fault: has_any_fault is defined for every joint", "[o30][sensor][fault]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.fault.get_blocking(std::chrono::milliseconds{500});
    for (size_t i = 0; i < kJointCount; ++i) {
        static_cast<void>(data.has_any_fault(i));
    }
}

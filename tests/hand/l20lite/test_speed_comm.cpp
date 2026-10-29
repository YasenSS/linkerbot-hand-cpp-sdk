#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::Side;
using linkerbot::hand::l20lite::Speed;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
}

// FakeTransport 速度：0x05 关节 1–5，0x06 关节 6–10；0–100% ↔ 0–255。

// 100/0/50% → 0x05[255,0,128,…]、0x06[128,…,0] 两帧。
TEST_CASE("comm: speed encodes percent across two frames", "[comm][l20lite][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x05, 5, 0x06, 5, 0));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.speed.set_speeds(Speed(100, 0, 50, 50, 50, 50, 50, 50, 50, 0));
    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x05, 255, 0, 128, 128, 128});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x06, 128, 128, 128, 128, 0});
}

// get 连发 0x05、0x06；填充 0x80 读回约 50%。
TEST_CASE("comm: speed get_blocking reads 0x05 and 0x06", "[comm][l20lite][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x05, 5, 0x06, 5, 0x80));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.speed.get_blocking(kTimeout);
    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x05});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x06});
    for (auto v : data.speed.to_array()) {
        REQUIRE(v == Catch::Approx(128.0f * 100.0f / 255.0f));
    }
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: speed snapshot populates after get", "[comm][l20lite][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId,
                          l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x05, 5, 0x06, 5, 0x40));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.speed.get_snapshot().has_value());
    (void)hand.speed.get_blocking(kTimeout);
    REQUIRE(hand.speed.get_snapshot().has_value());
}

// timeout<=0 应立刻抛。
TEST_CASE("comm: speed get rejects non-positive timeout", "[comm][l20lite][speed]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId,
                          l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x05, 5, 0x06, 5, 0));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.speed.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
}

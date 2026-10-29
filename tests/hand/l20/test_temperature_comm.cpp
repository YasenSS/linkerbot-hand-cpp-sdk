#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20/l20.hpp>

#include "hand/l20/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l20::L20;
using linkerbot::hand::l20::Side;
using linkerbot::hand::l20::Temperature;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
    constexpr uint8_t kTempCmd = 0x61;
    // raw uint8 即 ℃：0x25 → 37。
    constexpr uint8_t kFillC = 0x25;
} // namespace

// FakeTransport 温度：只读五帧 0x61–0x65；payload 原始字节直接当摄氏度。

// get 连发 DLC=1 的 0x61..0x65；填充 0x25 读回十六个 37℃。
TEST_CASE("comm: l20 temperature get_blocking reads 0x61 through 0x65", "[comm][l20][temperature]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kTempCmd, kFillC));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.temperature.get_blocking(kTimeout);
    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    REQUIRE(sent[sent.size() - 5] == std::vector<uint8_t>{0x61});
    REQUIRE(sent[sent.size() - 4] == std::vector<uint8_t>{0x62});
    REQUIRE(sent[sent.size() - 3] == std::vector<uint8_t>{0x63});
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x64});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x65});
    const auto joints = data.temperature.to_array();
    REQUIRE(joints.size() == Temperature::kJointCount);
    for (auto v : joints) {
        REQUIRE(v == Catch::Approx(37.f));
    }
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: l20 temperature snapshot populates after get", "[comm][l20][temperature]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kTempCmd, kFillC));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.temperature.get_snapshot().has_value());
    (void)hand.temperature.get_blocking(kTimeout);
    REQUIRE(hand.temperature.get_snapshot().has_value());
}

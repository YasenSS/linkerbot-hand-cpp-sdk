#include "hand/support/optional_value.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::kJointCount;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::protocol::kEmptyRequestBytes;
using linkerbot::hand::l30::protocol::kSubTemperature;
using linkerbot::hand::l30::protocol::kU8VectorPayloadBytes;
using linkerbot::hand::l30::protocol::ProtocolError;

namespace {
    constexpr auto kTimeout = l30_comm::kTimeout;

    // 温度查询 0x5/0x04/R。应答是 u8 向量：BYTE0=17，不是 i16 的 34。
    constexpr uint32_t kQueryId = 0x00A08100U;
    constexpr uint32_t kReportId = 0x00808008U;

    template <typename Ready> bool wait_until(Ready ready) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (ready()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return false;
    }

    // 17 个互不相同的温度，用来验证逐字节对位没错。
    std::array<uint8_t, kJointCount> sample_temperatures() {
        std::array<uint8_t, kJointCount> values{};
        for (size_t i = 0; i < kJointCount; ++i) {
            values[i] = static_cast<uint8_t>(20 + i);
        }
        return values;
    }
} // namespace

// FakeTransport 温度：只读，查询 0x00A08100 空载荷，应答是 17 字节 u8 向量
// （头 11 00），1 ℃ 每计数。

// get_blocking 发空载荷查询帧（BYTE0=0、DLC=2）并刷新 snapshot。
TEST_CASE("comm: temperature get_blocking sends empty query", "[comm][l30][temperature]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::u8_handler(sample_temperatures()));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_FALSE(hand.temperature.get_snapshot().has_value());
    const auto data = hand.temperature.get_blocking(kTimeout);

    const auto frames = l30_comm::frames_for(raw, kQueryId);
    REQUIRE(frames.size() == 1);
    REQUIRE(l30_comm::trimmed(frames.front()) == std::vector<uint8_t>{0x00, 0x00});
    REQUIRE(frames.front().dlc == kEmptyRequestBytes);
    REQUIRE(frames.front().is_extended);
    REQUIRE(frames.front().is_fd);
    REQUIRE(data.temperatures == sample_temperatures());
    REQUIRE(hand.temperature.get_snapshot().has_value());
    REQUIRE(test_support::require_value(hand.temperature.get_snapshot()).temperatures == sample_temperatures());
}

// 17 个值逐个对位解出，且应答声明的长度是 17（u8 向量），不是 34。
TEST_CASE("comm: temperature decodes all seventeen values", "[comm][l30][temperature]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::u8_handler(sample_temperatures()));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = hand.temperature.get_blocking(kTimeout);
    REQUIRE(data.temperatures.size() == kJointCount);
    for (size_t i = 0; i < kJointCount; ++i) {
        REQUIRE(data.temperatures[i] == static_cast<uint8_t>(20 + i));
    }
    REQUIRE(l30_comm::u8_response(sample_temperatures())[0] == kU8VectorPayloadBytes);
}

// u8 能表达的两端 0 和 255 都该原样读回。
TEST_CASE("comm: temperature decodes byte extremes", "[comm][l30][temperature]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    auto values = l30_comm::filled_u8(255);
    values[0] = 0;
    raw->on_request(kQueryId, l30_comm::u8_handler(values));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = hand.temperature.get_blocking(kTimeout);
    REQUIRE(data.temperatures[0] == 0);
    REQUIRE(data.temperatures[16] == 255);
}

// 温度周期上报（0x00808008，无状态字节的 u8 向量）不发请求也应刷新 snapshot。
TEST_CASE("comm: temperature periodic report updates snapshot", "[comm][l30][temperature]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto frame = l30_comm::periodic_u8_frame(kSubTemperature, sample_temperatures());
    REQUIRE(frame.id == kReportId);
    raw->inject(frame);

    REQUIRE(wait_until([&hand] { return hand.temperature.get_snapshot().has_value(); }));
    REQUIRE(test_support::require_value(hand.temperature.get_snapshot()).temperatures == sample_temperatures());
    REQUIRE(l30_comm::frames_for(raw, kQueryId).empty());
}

// timeout<=0 立刻抛 invalid_argument，不发帧。
TEST_CASE("comm: temperature get rejects non-positive timeout", "[comm][l30][temperature]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::u8_handler(l30_comm::filled_u8(25)));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.temperature.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.temperature.get_blocking(std::chrono::milliseconds{-7}), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// 非零状态字节 → ProtocolError，status() 带回原因码。
TEST_CASE("comm: temperature read surfaces device status", "[comm][l30][temperature]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::u8_handler(l30_comm::filled_u8(0), 0x41));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    uint8_t observed = 0xFFU;
    std::string message;
    try {
        (void)hand.temperature.get_blocking(kTimeout);
    } catch (const ProtocolError &error) {
        observed = error.status();
        message = error.what();
    }
    REQUIRE(observed == 0x41U);
    REQUIRE(message.find("communication lost") != std::string::npos);
}

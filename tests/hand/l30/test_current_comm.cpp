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
using linkerbot::hand::l30::kCurrentMilliampsPerCount;
using linkerbot::hand::l30::kJointCount;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::protocol::kEmptyRequestBytes;
using linkerbot::hand::l30::protocol::kSubCurrent;
using linkerbot::hand::l30::protocol::ProtocolError;

namespace {
    constexpr auto kTimeout = l30_comm::kTimeout;

    // 电流查询 0x5/0x02/R；写电流限制用 0x02204100，两者共用子命令 0x02。
    constexpr uint32_t kQueryId = 0x00A04100U;
    constexpr uint32_t kCurrentLimitWriteId = 0x02204100U;
    constexpr uint32_t kReportId = 0x00804008U;

    template <typename Ready> bool wait_until(Ready ready) {
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (ready()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return false;
    }

    // 电流是有符号的：符号表示电机出力方向，所以样本里必须有负数。
    std::array<int16_t, kJointCount> sample_currents() {
        return {0, 100, -100, 200, -200, 1, -1, 32767, -32768, 50, -50, 400, -400, 800, -800, 12, -12};
    }
} // namespace

// FakeTransport 电流：只读，查询 0x00A04100 空载荷，应答是 34 字节大端 i16；
// 6.5 mA 每计数由调用方换算，SDK 只给原始计数。

// get_blocking 发空载荷查询帧（BYTE0=0、DLC=2），解出 17 个计数并刷新 snapshot。
TEST_CASE("comm: current get_blocking sends empty query", "[comm][l30][current]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(sample_currents()));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_FALSE(hand.current.get_snapshot().has_value());
    const auto data = hand.current.get_blocking(kTimeout);

    const auto frames = l30_comm::frames_for(raw, kQueryId);
    REQUIRE(frames.size() == 1);
    REQUIRE(l30_comm::trimmed(frames.front()) == std::vector<uint8_t>{0x00, 0x00});
    REQUIRE(frames.front().dlc == kEmptyRequestBytes);
    REQUIRE(frames.front().is_extended);
    REQUIRE(frames.front().is_fd);
    REQUIRE(data.currents == sample_currents());
    REQUIRE(hand.current.get_snapshot().has_value());
    REQUIRE(hand.current.get_snapshot()->currents == sample_currents());
}

// 负计数按二进制补码解回：-100→FF9C，-32768→8000。
TEST_CASE("comm: current decodes negative counts", "[comm][l30][current]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(l30_comm::filled_i16(-1234)));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = hand.current.get_blocking(kTimeout);
    for (const auto counts : data.currents) {
        REQUIRE(counts == -1234);
    }
}

// 电流查询用 0x00A04100，绝不能走电流限制写入的 0x02204100。
TEST_CASE("comm: current query id differs from current-limit write", "[comm][l30][current]") {
    REQUIRE(l30_comm::query_id(kSubCurrent) == kQueryId);
    REQUIRE(kQueryId != kCurrentLimitWriteId);

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(l30_comm::filled_i16(7)));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    (void)hand.current.get_blocking(kTimeout);
    REQUIRE(l30_comm::count_id(raw, kQueryId) == 1);
    REQUIRE(l30_comm::count_id(raw, kCurrentLimitWriteId) == 0);
}

// kCurrentMilliampsPerCount=6.5：100 计数 = 650 mA，-200 计数 = -1300 mA。
TEST_CASE("comm: current counts convert to milliamps", "[comm][l30][current]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(sample_currents()));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto data = hand.current.get_blocking(kTimeout);
    REQUIRE(static_cast<float>(data.currents[1]) * kCurrentMilliampsPerCount == Catch::Approx(650.0F).margin(0.01));
    REQUIRE(static_cast<float>(data.currents[4]) * kCurrentMilliampsPerCount == Catch::Approx(-1300.0F).margin(0.01));
}

// 电流周期上报（0x00804008，无状态字节）不发请求也应刷新 snapshot。
TEST_CASE("comm: current periodic report updates snapshot", "[comm][l30][current]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    const auto frame = l30_comm::periodic_i16_frame(kSubCurrent, sample_currents());
    REQUIRE(frame.id == kReportId);
    raw->inject(frame);

    REQUIRE(wait_until([&hand] { return hand.current.get_snapshot().has_value(); }));
    REQUIRE(hand.current.get_snapshot()->currents == sample_currents());
    REQUIRE(l30_comm::frames_for(raw, kQueryId).empty());
}

// timeout<=0 立刻抛 invalid_argument，不发帧。
TEST_CASE("comm: current get rejects non-positive timeout", "[comm][l30][current]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(l30_comm::filled_i16(0)));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    REQUIRE_THROWS_AS(hand.current.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.current.get_blocking(std::chrono::milliseconds{-3}), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

// 非零状态字节 → ProtocolError，status() 带回原因码。
TEST_CASE("comm: current read surfaces device status", "[comm][l30][current]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kQueryId, l30_comm::i16_handler(l30_comm::filled_i16(0), 0x30));
    L30 hand(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);

    uint8_t observed = 0xFFU;
    std::string message;
    try {
        (void)hand.current.get_blocking(kTimeout);
    } catch (const ProtocolError &error) {
        observed = error.status();
        message = error.what();
    }
    REQUIRE(observed == 0x30U);
    REQUIRE(message.find("motor data read error") != std::string::npos);
}

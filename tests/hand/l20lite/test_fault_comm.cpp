#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20lite/fault.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l20lite::Fault;
using linkerbot::hand::l20lite::FaultCode;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::Side;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
}

// 0x35/0x36 两帧错误码；BIT0 电压、BIT1 磁编码、BIT2 温度、BIT3 电流、BIT5 过载；无 0x83。

// from_raw：BIT0/1/2/3/5 依次解成电压/磁编/温度/电流/过载；0xD0 未用位应掩成 kNone。
TEST_CASE("comm: fault from_raw masks unused bits", "[comm][l20lite][fault]") {
    const auto fault = Fault::from_raw({0x01, 0x02, 0x04, 0x08, 0x20, 0xD0, 0, 0, 0, 0});
    REQUIRE(fault.thumb_flex == FaultCode::kVoltage);
    REQUIRE(fault.thumb_abd == FaultCode::kEncoder);
    REQUIRE(fault.index_flex == FaultCode::kOvertemp);
    REQUIRE(fault.middle_flex == FaultCode::kOvercurrent);
    REQUIRE(fault.ring_flex == FaultCode::kOverload);
    REQUIRE(fault.pinky_flex == FaultCode::kNone);
    REQUIRE(fault.has_any_fault());
}

// get_blocking 连发 0x35、0x36；填充 0x01 时拇指屈曲/偏航都是电压故障。
TEST_CASE("comm: fault get_blocking sends 0x35 and 0x36", "[comm][l20lite][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x35, 5, 0x36, 5, 0x01));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.fault.get_blocking(kTimeout);
    const auto sent = l20lite_comm::payloads(raw);
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x35});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x36});
    REQUIRE(data.fault.thumb_flex == FaultCode::kVoltage);
    REQUIRE(data.fault.thumb_yaw == FaultCode::kVoltage);
    REQUIRE(data.fault.has_any_fault());
}

// 读故障只发 0x35/0x36，线上不应出现 0x83。
TEST_CASE("comm: fault never transmits 0x83", "[comm][l20lite][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x35, 5, 0x36, 5, 0));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    (void)hand.fault.get_blocking(kTimeout);

    for (const auto &msg : raw->sent()) {
        REQUIRE_FALSE((msg.dlc >= 1 && msg.data[0] == 0x83));
    }
}

// 0x36 是第二组故障查询（单字节请求），不是电流命令。
TEST_CASE("comm: 0x36 is fault bank 2 not current", "[comm][l20lite][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, l20lite_comm::dual_handler(l20lite_comm::kLeftId, 0x35, 5, 0x36, 5, 0));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    (void)hand.fault.get_blocking(kTimeout);

    bool saw_36 = false;
    for (const auto &msg : raw->sent()) {
        if (msg.dlc >= 1 && msg.data[0] == 0x36) {
            saw_36 = true;
            REQUIRE(msg.dlc == 1);
        }
    }
    REQUIRE(saw_36);
}

TEST_CASE("comm: L20Lite fault banks respect a one millisecond device turnaround", "[comm][l20lite][fault][pacing]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    auto first_at = std::chrono::steady_clock::time_point{};
    raw->on_request(l20lite_comm::kLeftId, [&](const linkerbot::can::Message &request) {
        const auto now = std::chrono::steady_clock::now();
        if (request.data[0] == 0x35) {
            first_at = now;
        } else if (request.data[0] != 0x36 || now - first_at < std::chrono::milliseconds{1}) {
            return std::vector<linkerbot::can::Message>{};
        }
        auto reply = request;
        reply.dlc = 6;
        return std::vector<linkerbot::can::Message>{reply};
    });
    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    REQUIRE_FALSE(hand.fault.get_blocking(kTimeout).fault.has_any_fault());
    REQUIRE(raw->sent().size() == 2);
}

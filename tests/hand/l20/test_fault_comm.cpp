#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20/fault.hpp>
#include <linkerbot/hand/l20/l20.hpp>

#include "hand/l20/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l20::Fault;
using linkerbot::hand::l20::L20;
using linkerbot::hand::l20::L20FaultCode;
using linkerbot::hand::l20::Side;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
    constexpr uint8_t kFaultCmd = 0x59;
} // namespace

// FakeTransport 故障：五帧 0x59–0x5D 查询；clear 每帧 [cmd,1,1,1,1,1,1]。

// get_blocking 连发 DLC=1 的 0x59..0x5D；填充 0x01 经 from_raw 成转子锁。
TEST_CASE("comm: l20 fault get_blocking sends 0x59 through 0x5D", "[comm][l20][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kFaultCmd, 0x01));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.fault.get_blocking(kTimeout);
    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    REQUIRE(sent[sent.size() - 5] == std::vector<uint8_t>{0x59});
    REQUIRE(sent[sent.size() - 4] == std::vector<uint8_t>{0x5A});
    REQUIRE(sent[sent.size() - 3] == std::vector<uint8_t>{0x5B});
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x5C});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x5D});
    REQUIRE(data.fault.to_array().size() == Fault::kJointCount);
    REQUIRE(data.fault.thumb_abd == L20FaultCode::kMotorRotorLock);
    REQUIRE(data.fault.thumb_yaw == L20FaultCode::kMotorRotorLock);
    REQUIRE(data.fault.pinky_tip == L20FaultCode::kMotorRotorLock);
    REQUIRE(data.fault.has_any_fault());
}

// clear_faults 对 0x59–0x5D 各发一帧 [cmd, 1, 1, 1, 1, 1, 1]。
TEST_CASE("comm: l20 clear_faults writes ones on five frames", "[comm][l20][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kFaultCmd, 0));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    raw->clear_sent();

    hand.fault.clear_faults();
    const auto sent = l20_comm::payloads(raw);
    REQUIRE(sent.size() >= 5);
    REQUIRE(sent[sent.size() - 5] == std::vector<uint8_t>{0x59, 1, 1, 1, 1, 1, 1});
    REQUIRE(sent[sent.size() - 4] == std::vector<uint8_t>{0x5A, 1, 1, 1, 1, 1, 1});
    REQUIRE(sent[sent.size() - 3] == std::vector<uint8_t>{0x5B, 1, 1, 1, 1, 1, 1});
    REQUIRE(sent[sent.size() - 2] == std::vector<uint8_t>{0x5C, 1, 1, 1, 1, 1, 1});
    REQUIRE(sent.back() == std::vector<uint8_t>{0x5D, 1, 1, 1, 1, 1, 1});
}

// get 成功后 snapshot 有值。
TEST_CASE("comm: l20 fault snapshot populates after get", "[comm][l20][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, kFaultCmd, 0));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.fault.get_snapshot().has_value());
    (void)hand.fault.get_blocking(kTimeout);
    REQUIRE(hand.fault.get_snapshot().has_value());
}

TEST_CASE("comm: L20 clear faults permits a followup query with MCU write pacing", "[comm][l20][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, [last_write = std::optional<std::chrono::steady_clock::time_point>{}](
                                                 const linkerbot::can::Message &request) mutable {
        if (request.data[0] < 0x59 || request.data[0] > 0x5D) {
            return std::vector<linkerbot::can::Message>{};
        }
        if (request.dlc == 7) {
            const auto now = std::chrono::steady_clock::now();
            const bool busy = last_write && now - *last_write < std::chrono::milliseconds{1};
            last_write = now;
            if (busy) {
                return std::vector<linkerbot::can::Message>{};
            }
        }
        linkerbot::can::Message reply;
        reply.id = request.id;
        reply.dlc = 7;
        reply.data[0] = request.data[0];
        return std::vector<linkerbot::can::Message>{reply};
    });
    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.fault.clear_faults();
    const auto faults = hand.fault.get_blocking(kTimeout);
    REQUIRE_FALSE(faults.fault.has_any_fault());
}

#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/o6/fault.hpp>
#include <linkerbot/hand/o6/o6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::o6::Fault;
using linkerbot::hand::o6::FaultCode;
using linkerbot::hand::o6::O6;
using linkerbot::hand::o6::Side;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kReadFaultCmd = 0x35;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    std::vector<uint8_t> last_payload(FakeTransport *transport) {
        const auto sent = transport->sent();
        REQUIRE_FALSE(sent.empty());
        const auto &msg = sent.back();
        return {msg.data.begin(), msg.data.begin() + msg.dlc};
    }

    FakeTransport::Handler fault_handler(uint32_t id, const std::array<uint8_t, 6> &codes) {
        return [id, codes](const Message &req) {
            if (req.dlc < 1 || req.data[0] != kReadFaultCmd) {
                return std::vector<Message>{};
            }
            Message reply;
            reply.id = id;
            reply.dlc = 7;
            reply.data[0] = kReadFaultCmd;
            for (size_t i = 0; i < 6; ++i) {
                reply.data[i + 1] = codes[i];
            }
            return std::vector<Message>{reply};
        };
    }

} // namespace

// 新协议 0x35：BIT0 电压、BIT1 磁编码、BIT2 温度、BIT3 电流、BIT5 过载；无 0x83 清故障。

TEST_CASE("comm: fault from_raw masks unused bits", "[comm][o6][fault]") {
    const auto fault = Fault::from_raw({0x01, 0x02, 0x04, 0x08, 0x20, 0xD0});
    REQUIRE(fault.thumb_flex == FaultCode::kVoltage);
    REQUIRE(fault.thumb_abd == FaultCode::kEncoder);
    REQUIRE(fault.index == FaultCode::kOvertemp);
    REQUIRE(fault.middle == FaultCode::kOvercurrent);
    REQUIRE(fault.ring == FaultCode::kOverload);
    REQUIRE(fault.pinky == FaultCode::kNone);
    REQUIRE(fault.has_any_fault());
}

TEST_CASE("comm: fault get_blocking sends 0x35 and decodes bits", "[comm][o6][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, fault_handler(kLeftId, {0x01, 0, 0, 0, 0, 0}));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    const auto data = hand.fault.get_blocking(kTimeout);
    REQUIRE(last_payload(raw) == std::vector<uint8_t>{kReadFaultCmd});
    REQUIRE(data.fault.thumb_flex == FaultCode::kVoltage);
    REQUIRE(data.fault.has_any_fault());
}

TEST_CASE("comm: fault snapshot populates after get", "[comm][o6][fault]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, fault_handler(kLeftId, {0, 0, 0, 0, 0, 0}));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_FALSE(hand.fault.get_snapshot().has_value());
    (void)hand.fault.get_blocking(kTimeout);
    REQUIRE(hand.fault.get_snapshot().has_value());
    REQUIRE_FALSE(test_support::require_value(hand.fault.get_snapshot()).fault.has_any_fault());
}

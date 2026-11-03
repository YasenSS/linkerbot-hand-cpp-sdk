#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <thread>
#include <variant>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l20/events.hpp>
#include <linkerbot/hand/l20/l20.hpp>

#include "hand/l20/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l20::ForceSensorEvent;
using linkerbot::hand::l20::L20;
using linkerbot::hand::l20::SensorEvent;
using linkerbot::hand::l20::SensorSource;
using linkerbot::hand::l20::Side;

namespace {

    constexpr uint8_t kFill = 0x11;
    constexpr uint8_t kRequestByte = 0xC6;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    std::vector<Message> force_frames(uint32_t id, uint8_t prefix, uint8_t fill) {
        std::vector<Message> frames;
        frames.reserve(12);
        for (uint8_t i = 0; i < 12; ++i) {
            Message msg;
            msg.id = id;
            msg.dlc = 8;
            msg.data[0] = prefix;
            msg.data[1] = static_cast<uint8_t>(i << 4);
            for (uint8_t j = 0; j < 6; ++j) {
                msg.data[2 + j] = fill;
            }
            frames.push_back(msg);
        }
        return frames;
    }

    FakeTransport::Handler force_handler(uint32_t id) {
        return [id](const Message &req) {
            if (req.dlc < 2 || req.data[1] != kRequestByte) {
                return std::vector<Message>{};
            }
            if (req.data[0] < 0xB1 || req.data[0] > 0xB5) {
                return std::vector<Message>{};
            }
            return force_frames(id, req.data[0], kFill);
        };
    }

    std::vector<SensorEvent> collect_events(L20 &hand, std::chrono::milliseconds duration) {
        std::vector<SensorEvent> collected;
        auto &queue = hand.stream();
        std::thread collector([&] {
            for (const auto &event : queue) {
                collected.push_back(event);
            }
        });
        std::this_thread::sleep_for(duration);
        hand.stop_stream();
        collector.join();
        return collected;
    }

} // namespace

// 点阵：BYTE1=0xC6 表示 12 行 × 6 列；轮询前缀 0xB1–0xB5。

// 轮询应向五指各发一帧：前缀 0xB1–0xB5，BYTE1=0xC6。
TEST_CASE("comm: l20 force sensor polling sends all finger requests", "[comm][l20][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, force_handler(l20_comm::kLeftId));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    raw->clear_sent();

    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{40});
    std::this_thread::sleep_for(std::chrono::milliseconds{120});
    hand.stop_polling();

    std::set<uint8_t> prefixes;
    for (const auto &msg : raw->sent()) {
        if (msg.dlc == 2 && msg.data[1] == kRequestByte) {
            prefixes.insert(msg.data[0]);
        }
    }
    REQUIRE(prefixes == std::set<uint8_t>{0xB1, 0xB2, 0xB3, 0xB4, 0xB5});
}

// 轮询后快照有点阵；拇指 [0][0]、小指 [11][5] 都应是填充值。
TEST_CASE("comm: l20 force sensor polling populates snapshot", "[comm][l20][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, force_handler(l20_comm::kLeftId));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{40});
    std::this_thread::sleep_for(kTimeout);
    hand.stop_polling();

    const auto snap = hand.get_snapshot();
    REQUIRE(snap.force_sensor.has_value());
    REQUIRE(test_support::require_value(snap.force_sensor).thumb.values[0][0] == kFill);
    REQUIRE(test_support::require_value(snap.force_sensor).pinky.values[11][5] == kFill);
}

// 轮询期间数据流应吐出 ForceSensorEvent，食指 [0][0] 为填充值。
TEST_CASE("comm: l20 force sensor stream emits ForceSensorEvent", "[comm][l20][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20_comm::kLeftId, force_handler(l20_comm::kLeftId));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{40});

    const auto events = collect_events(hand, kTimeout);
    hand.stop_polling();

    REQUIRE_FALSE(events.empty());
    bool has_force = false;
    for (const auto &event : events) {
        if (std::holds_alternative<ForceSensorEvent>(event)) {
            has_force = true;
            REQUIRE(std::get<ForceSensorEvent>(event).data.index.values[0][0] == kFill);
        }
    }
    REQUIRE(has_force);
}

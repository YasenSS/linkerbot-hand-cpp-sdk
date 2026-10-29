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
#include <linkerbot/hand/o6/events.hpp>
#include <linkerbot/hand/o6/o6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::o6::ForceSensorEvent;
using linkerbot::hand::o6::O6;
using linkerbot::hand::o6::SensorEvent;
using linkerbot::hand::o6::SensorSource;
using linkerbot::hand::o6::Side;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kFill = 0x11;
    constexpr uint8_t kRequestByte = 0xA4;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    std::vector<Message> force_frames(uint32_t id, uint8_t prefix, uint8_t fill) {
        std::vector<Message> frames;
        frames.reserve(10);
        for (uint8_t i = 0; i < 10; ++i) {
            Message msg;
            msg.id = id;
            msg.dlc = 6;
            msg.data[0] = prefix;
            msg.data[1] = static_cast<uint8_t>(i << 4);
            for (uint8_t j = 0; j < 4; ++j) {
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

    std::vector<SensorEvent> collect_events(O6 &hand, std::chrono::milliseconds duration) {
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

// 新协议点阵：BYTE1=0xA4 表示 10 行 × 4 列。

TEST_CASE("comm: force sensor polling sends all finger requests", "[comm][o6][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, force_handler(kLeftId));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
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

TEST_CASE("comm: force sensor polling populates snapshot", "[comm][o6][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, force_handler(kLeftId));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{40});
    std::this_thread::sleep_for(kTimeout);
    hand.stop_polling();

    const auto snap = hand.get_snapshot();
    REQUIRE(snap.force_sensor.has_value());
    REQUIRE(snap.force_sensor->thumb.values[0][0] == kFill);
    REQUIRE(snap.force_sensor->pinky.values[9][3] == kFill);
}

TEST_CASE("comm: force sensor stream emits ForceSensorEvent", "[comm][o6][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, force_handler(kLeftId));

    O6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{40});

    const auto events = collect_events(hand, kTimeout);
    hand.stop_polling();

    REQUIRE_FALSE(events.empty());
    bool has_force = false;
    for (const auto &event : events) {
        if (std::holds_alternative<ForceSensorEvent>(event)) {
            has_force = true;
            const auto &data = std::get<ForceSensorEvent>(event).data;
            REQUIRE(data.index.values[0][0] == kFill);
        }
    }
    REQUIRE(has_force);
}

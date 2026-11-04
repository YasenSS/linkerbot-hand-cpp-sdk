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
#include <linkerbot/hand/l6/events.hpp>
#include <linkerbot/hand/l6/l6.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l6::ForceSensorEvent;
using linkerbot::hand::l6::L6;
using linkerbot::hand::l6::SensorEvent;
using linkerbot::hand::l6::SensorSource;
using linkerbot::hand::l6::Side;

namespace {

    constexpr uint32_t kLeftId = 0x28;
    constexpr uint8_t kFill = 0x11;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    struct ForceFramesArguments {
        uint32_t id;
        uint8_t prefix;
        uint8_t fill;
    };
    std::vector<Message> force_frames(const ForceFramesArguments &arguments) {
        std::vector<Message> frames;
        frames.reserve(12);
        for (uint8_t i = 0; i < 12; ++i) {
            Message msg;
            msg.id = arguments.id;
            msg.dlc = 8;
            msg.data[0] = arguments.prefix;
            msg.data[1] = static_cast<uint8_t>(i << 4);
            for (uint8_t j = 0; j < 6; ++j) {
                msg.data[2 + j] = arguments.fill;
            }
            frames.push_back(msg);
        }
        return frames;
    }

    FakeTransport::Handler force_handler(uint32_t id) {
        return [id](const Message &req) {
            if (req.dlc < 2 || req.data[1] != 0xC6) {
                return std::vector<Message>{};
            }
            if (req.data[0] < 0xB1 || req.data[0] > 0xB5) {
                return std::vector<Message>{};
            }
            return force_frames({id, req.data[0], kFill});
        };
    }

    std::vector<SensorEvent> collect_events(L6 &hand, std::chrono::milliseconds duration) {
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

TEST_CASE("comm: force sensor polling sends all finger requests", "[comm][l6][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, force_handler(kLeftId));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    raw->clear_sent();

    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{40});
    std::this_thread::sleep_for(std::chrono::milliseconds{120});
    hand.stop_polling();

    std::set<uint8_t> prefixes;
    for (const auto &msg : raw->sent()) {
        if (msg.dlc == 2 && msg.data[1] == 0xC6) {
            prefixes.insert(msg.data[0]);
        }
    }
    REQUIRE(prefixes == std::set<uint8_t>{0xB1, 0xB2, 0xB3, 0xB4, 0xB5});
}

TEST_CASE("comm: force sensor polling populates snapshot", "[comm][l6][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, force_handler(kLeftId));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{40});
    std::this_thread::sleep_for(kTimeout);
    hand.stop_polling();

    const auto snap = hand.get_snapshot();
    REQUIRE(snap.force_sensor.has_value());
    REQUIRE(test_support::require_value(snap.force_sensor).thumb.values[0][0] == kFill);
    REQUIRE(test_support::require_value(snap.force_sensor).pinky.values[11][5] == kFill);
}

TEST_CASE("comm: force sensor stream emits ForceSensorEvent", "[comm][l6][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, force_handler(kLeftId));

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
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

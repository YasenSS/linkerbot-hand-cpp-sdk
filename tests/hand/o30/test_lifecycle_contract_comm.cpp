#include "hand/o30/comm_helpers.hpp"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <thread>
#include <variant>

using namespace linkerbot::hand::o30;
using namespace std::chrono_literals;

TEST_CASE("comm: O30i every polling source reaches the correct snapshot and event", "[comm][o30][alignment][polling]") {
    const bool modern = GENERATE(false, true);
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(0x41, 0x61, 8, modern ? "0.0.4" : "0.0.3");
    constexpr std::array<uint8_t, 20> slots{0, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    // Distinct objects catch accidental routing to another manager or event.
    std::array<std::array<uint8_t, 20>, 9> expected{};
    for (size_t i = 0; i < expected.size(); ++i) {
        const auto mi = static_cast<uint8_t>(i == 8 ? (modern ? 0x0C : 0x0D) : (i < 7 ? i + 1 : 0x09));
        auto wire = std::vector<uint8_t>(36);
        for (size_t j = 0; j < wire.size(); ++j) {
            wire[j] = static_cast<uint8_t>(i * 20 + j);
        }
        for (size_t j = 0; j < slots.size(); ++j) {
            expected[i][j] = wire[slots[j]];
        }
        h.hop->put(mi, wire);
    }
    auto &queue = h.hand->stream(100);
    h.hand->start_polling({
        {SensorSource::kAngle, 20ms},
        {SensorSource::kSpeed, 20ms},
        {SensorSource::kAcceleration, 20ms},
        {SensorSource::kCurrent, 20ms},
        {SensorSource::kVoltage, 20ms},
        {SensorSource::kTorque, 20ms},
        {SensorSource::kTemperature, 20ms},
        {SensorSource::kStallTime, 20ms},
        {SensorSource::kFault, 20ms},
    });
    const auto complete = [](const Snapshot &s) {
        return s.angle && s.speed && s.acceleration && s.current && s.voltage && s.torque && s.temperature &&
               s.stall_time && s.fault;
    };
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!complete(h.hand->get_snapshot()) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    h.hand->stop_polling();
    h.hand->stop_stream(); // Close before draining so a missing event cannot hang the suite.
    const auto snapshot = h.hand->get_snapshot();
    REQUIRE(complete(snapshot));
    REQUIRE(snapshot.angle->angles.to_raw() == expected[0]);
    REQUIRE(snapshot.speed->speeds == expected[1]);
    REQUIRE(snapshot.acceleration->accelerations == expected[2]);
    REQUIRE(snapshot.current->currents == expected[3]);
    REQUIRE(snapshot.voltage->voltages == expected[4]);
    REQUIRE(snapshot.torque->torques == expected[5]);
    REQUIRE(snapshot.temperature->temperatures == expected[6]);
    REQUIRE(snapshot.stall_time->times == expected[7]);
    REQUIRE(snapshot.fault->faults == expected[8]);
    std::array<bool, 9> seen{};
    while (const auto event = queue.pop()) {
        if (const auto *e = std::get_if<AngleEvent>(&*event)) {
            REQUIRE(e->data.angles.to_raw() == expected[0]);
            REQUIRE(e->data.kind == ValueKind::kActual);
            REQUIRE(e->data.timestamp.time_since_epoch().count() > 0);
            seen[0] = true;
        }
        if (const auto *e = std::get_if<SpeedEvent>(&*event)) {
            REQUIRE(e->data.speeds == expected[1]);
            REQUIRE(e->data.kind == ValueKind::kActual);
            REQUIRE(e->data.timestamp.time_since_epoch().count() > 0);
            seen[1] = true;
        }
        if (const auto *e = std::get_if<AccelerationEvent>(&*event)) {
            REQUIRE(e->data.accelerations == expected[2]);
            REQUIRE(e->data.kind == ValueKind::kActual);
            REQUIRE(e->data.timestamp.time_since_epoch().count() > 0);
            seen[2] = true;
        }
        if (const auto *e = std::get_if<CurrentEvent>(&*event)) {
            REQUIRE(e->data.currents == expected[3]);
            REQUIRE(e->data.timestamp.time_since_epoch().count() > 0);
            seen[3] = true;
        }
        if (const auto *e = std::get_if<VoltageEvent>(&*event)) {
            REQUIRE(e->data.voltages == expected[4]);
            REQUIRE(e->data.kind == ValueKind::kActual);
            REQUIRE(e->data.timestamp.time_since_epoch().count() > 0);
            seen[4] = true;
        }
        if (const auto *e = std::get_if<TorqueEvent>(&*event)) {
            REQUIRE(e->data.torques == expected[5]);
            REQUIRE(e->data.timestamp.time_since_epoch().count() > 0);
            seen[5] = true;
        }
        if (const auto *e = std::get_if<TemperatureEvent>(&*event)) {
            REQUIRE(e->data.temperatures == expected[6]);
            REQUIRE(e->data.kind == ValueKind::kActual);
            REQUIRE(e->data.timestamp.time_since_epoch().count() > 0);
            seen[6] = true;
        }
        if (const auto *e = std::get_if<StallTimeEvent>(&*event)) {
            REQUIRE(e->data.times == expected[7]);
            REQUIRE(e->data.kind == ValueKind::kActual);
            REQUIRE(e->data.timestamp.time_since_epoch().count() > 0);
            seen[7] = true;
        }
        if (const auto *e = std::get_if<FaultEvent>(&*event)) {
            REQUIRE(e->data.faults == expected[8]);
            REQUIRE(e->data.kind == ValueKind::kActual);
            REQUIRE(e->data.timestamp.time_since_epoch().count() > 0);
            seen[8] = true;
        }
    }
    for (const auto present : seen) {
        REQUIRE(present);
    }
}

#include "hand/o30/comm_helpers.hpp"
#include "hand/support/optional_value.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <functional>
#include <future>
#include <linkerbot/hand/errors.hpp>

using namespace linkerbot::hand::o30;
using namespace std::chrono_literals;

namespace {
    using Values = std::array<uint8_t, 20>;
    constexpr std::array<uint8_t, 20> slots{0, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    struct RuntimeContract {
        uint8_t mi;
        std::function<Values(O30 &, ValueKind, std::chrono::milliseconds)> read;
        std::function<std::optional<Values>(O30 &, ValueKind)> snapshot;
        std::function<void(O30 &, const Values &, std::chrono::milliseconds)> write;
        std::function<void(O30 &, uint8_t)> write_all;
    };
    const std::array<RuntimeContract, 8> contracts{{
        {0x02,
         [](O30 &h, ValueKind k, auto t) {
             const auto data = h.speed.get_blocking(t, k);
             REQUIRE(data.kind == k);
             REQUIRE(data.timestamp.time_since_epoch().count() > 0);
             return data.speeds;
         },
         [](O30 &h, ValueKind) {
             const auto data = h.speed.get_snapshot();
             return data ? std::optional<Values>{data->speeds} : std::nullopt;
         },
         [](O30 &h, const Values &v, auto t) { h.speed.set_speeds(v, t); },
         [](O30 &h, uint8_t v) { h.speed.set_all(v, 500ms); }},
        {0x03,
         [](O30 &h, ValueKind k, auto t) {
             const auto data = h.acceleration.get_blocking(t, k);
             REQUIRE(data.kind == k);
             REQUIRE(data.timestamp.time_since_epoch().count() > 0);
             return data.accelerations;
         },
         [](O30 &h, ValueKind) {
             const auto data = h.acceleration.get_snapshot();
             return data ? std::optional<Values>{data->accelerations} : std::nullopt;
         },
         [](O30 &h, const Values &v, auto t) { h.acceleration.set_accelerations(v, t); },
         [](O30 &h, uint8_t v) { h.acceleration.set_all(v, 500ms); }},
        {0x04,
         [](O30 &h, ValueKind k, auto t) {
             if (k == ValueKind::kActual) {
                 const auto data = h.current.get_currents(t);
                 REQUIRE(data.timestamp.time_since_epoch().count() > 0);
                 return data.currents;
             }
             const auto data = h.current.get_current_limits(t);
             REQUIRE(data.timestamp.time_since_epoch().count() > 0);
             return data.current_limits;
         },
         [](O30 &h, ValueKind k) {
             if (k == ValueKind::kActual) {
                 const auto data = h.current.get_snapshot();
                 return data ? std::optional<Values>{data->currents} : std::nullopt;
             }
             const auto data = h.current.get_current_limit_snapshot();
             return data ? std::optional<Values>{data->current_limits} : std::nullopt;
         },
         [](O30 &h, const Values &v, auto t) { h.current.set_current_limits(v, t); },
         [](O30 &h, uint8_t v) { h.current.set_all_current_limits(v, 500ms); }},
        {0x05,
         [](O30 &h, ValueKind k, auto t) {
             const auto data = h.voltage.get_blocking(t, k);
             REQUIRE(data.kind == k);
             REQUIRE(data.timestamp.time_since_epoch().count() > 0);
             return data.voltages;
         },
         [](O30 &h, ValueKind) {
             const auto data = h.voltage.get_snapshot();
             return data ? std::optional<Values>{data->voltages} : std::nullopt;
         },
         [](O30 &h, const Values &v, auto t) { h.voltage.set_voltages(v, t); },
         [](O30 &h, uint8_t v) { h.voltage.set_all(v, 500ms); }},
        {0x07,
         [](O30 &h, ValueKind k, auto t) {
             const auto data = h.temperature.get_blocking(t, k);
             REQUIRE(data.kind == k);
             REQUIRE(data.timestamp.time_since_epoch().count() > 0);
             return data.temperatures;
         },
         [](O30 &h, ValueKind) {
             const auto data = h.temperature.get_snapshot();
             return data ? std::optional<Values>{data->temperatures} : std::nullopt;
         },
         [](O30 &h, const Values &v, auto t) { h.temperature.set_temperatures(v, t); },
         {}},
        {0x09,
         [](O30 &h, ValueKind k, auto t) {
             const auto data = h.stall.get_times(t, k);
             REQUIRE(data.kind == k);
             REQUIRE(data.timestamp.time_since_epoch().count() > 0);
             return data.times;
         },
         [](O30 &h, ValueKind) {
             const auto data = h.stall.get_times_snapshot();
             return data ? std::optional<Values>{data->times} : std::nullopt;
         },
         [](O30 &h, const Values &v, auto t) { h.stall.set_times(v, t); },
         {}},
        {0x0A,
         [](O30 &h, ValueKind k, auto t) {
             const auto data = h.stall.get_thresholds(t, k);
             REQUIRE(data.kind == k);
             REQUIRE(data.timestamp.time_since_epoch().count() > 0);
             return data.thresholds;
         },
         [](O30 &h, ValueKind) {
             const auto data = h.stall.get_thresholds_snapshot();
             return data ? std::optional<Values>{data->thresholds} : std::nullopt;
         },
         [](O30 &h, const Values &v, auto t) { h.stall.set_thresholds(v, t); },
         {}},
        {0x0B,
         [](O30 &h, ValueKind k, auto t) {
             const auto data = h.stall.get_hold_currents(t, k);
             REQUIRE(data.kind == k);
             REQUIRE(data.timestamp.time_since_epoch().count() > 0);
             return data.currents;
         },
         [](O30 &h, ValueKind) {
             const auto data = h.stall.get_hold_currents_snapshot();
             return data ? std::optional<Values>{data->currents} : std::nullopt;
         },
         [](O30 &h, const Values &v, auto t) { h.stall.set_hold_currents(v, t); },
         {}},
    }};
}

// Every byte-valued runtime API must project only the 20 assembled joints,
// distinguish actual/setpoint, and publish the exact successfully decoded value.
TEST_CASE("comm: O30i all runtime managers obey read and snapshot contracts", "[comm][o30][alignment][managers]") {
    const auto index = GENERATE(Catch::Generators::range(size_t{0}, contracts.size()));
    const auto kind = GENERATE(ValueKind::kActual, ValueKind::kSetpoint);
    const auto &api = contracts[index];
    INFO("MI=" << +api.mi << ", setpoint=" << (kind == ValueKind::kSetpoint));
    auto h = o30_comm::make_hand();
    auto actual = o30_comm::ramp_slots();
    auto setpoint = actual;
    for (auto &v : setpoint) {
        v += 100;
    }
    h.hop->put(api.mi, actual);
    h.hop->put(api.mi, setpoint, true);
    REQUIRE_FALSE(api.snapshot(*h.hand, kind).has_value());
    const auto values = api.read(*h.hand, kind, 500ms);
    for (size_t j = 0; j < 20; ++j) {
        REQUIRE(values[j] == (kind == ValueKind::kActual ? actual : setpoint)[slots[j]]);
    }
    REQUIRE(api.snapshot(*h.hand, kind) == values);
    REQUIRE(o30_comm::last_payload(h.transport) ==
            std::vector<uint8_t>{static_cast<uint8_t>(api.mi | (kind == ValueKind::kSetpoint ? 0x80 : 0)), 0, 36});

    // A short wire object must not replace a valid snapshot with zero-filled data.
    h.transport->on_request(1, [mi = api.mi, kind](const auto &) {
        return std::vector<linkerbot::can::Message>{
            o30_comm::hop_reply(0x401, mi, kind == ValueKind::kSetpoint, 0, std::vector<uint8_t>(35, 0))};
    });
    REQUIRE_THROWS_AS(api.read(*h.hand, kind, 500ms), protocol::ProtocolError);
    REQUIRE(api.snapshot(*h.hand, kind) == values);
}

TEST_CASE("comm: O30i every runtime writer skips reserved slots with correct RTS", "[comm][o30][alignment][managers]") {
    const auto index = GENERATE(Catch::Generators::range(size_t{0}, contracts.size()));
    const auto modern = GENERATE(false, true);
    const auto &api = contracts[index];
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(0x41, 0x61, 8, modern ? "0.0.4" : "0.0.3");
    h.hop->put(api.mi, std::vector<uint8_t>(36, 0xA5));
    Values values{};
    for (size_t i = 0; i < 20; ++i) {
        values[i] = static_cast<uint8_t>(3 + 11 * i);
    }
    api.write(*h.hand, values, 500ms);
    auto expected = std::vector<uint8_t>(36, 0xA5);
    for (size_t j = 0; j < 20; ++j) {
        expected[slots[j]] = values[j];
    }
    REQUIRE(h.hop->objects.at({api.mi, false}) == expected);
    REQUIRE(h.hop->writes.size() == 3);
    const uint8_t ctrl = static_cast<uint8_t>(api.mi | (modern && api.mi == 4 ? 0x80 : 0));
    size_t writes = 0;
    for (const auto &frame : h.transport->sent()) {
        if (frame.dlc == 3) {
            continue; // revision discovery, when needed
        }
        REQUIRE(frame.data[0] == ctrl);
        constexpr uint8_t si[]{0, 5, 16};
        constexpr uint8_t edl[]{1, 10, 9};
        REQUIRE(frame.data[1] == si[writes]);
        REQUIRE(frame.data[2] == edl[writes]);
        ++writes;
    }
    REQUIRE(writes == 3);
    if (api.write_all) {
        h.hop->writes.clear();
        api.write_all(*h.hand, 255);
        REQUIRE(h.hop->writes.size() == 3);
        for (const auto &write : h.hop->writes) {
            for (const auto v : write.payload) {
                REQUIRE(v == 255);
            }
        }
    }
}

TEST_CASE("comm: O30i runtime APIs reject invalid timeouts without any I/O", "[comm][o30][alignment][managers]") {
    const auto index = GENERATE(Catch::Generators::range(size_t{0}, contracts.size()));
    const auto timeout = GENERATE(0ms, -1ms);
    const auto &api = contracts[index];
    auto h = o30_comm::make_hand();
    REQUIRE_THROWS_AS(api.read(*h.hand, ValueKind::kActual, timeout), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(api.write(*h.hand, Values{}, timeout), linkerbot::hand::ValidationError);
    REQUIRE(h.transport->sent().empty());
}

TEST_CASE("comm: O30i all six PID terms use byte objects and safe write segments", "[comm][o30][alignment][pid]") {
    const auto mi = GENERATE(uint8_t{0x21}, uint8_t{0x22}, uint8_t{0x23}, uint8_t{0x24}, uint8_t{0x25}, uint8_t{0x26});
    const auto kind = GENERATE(ValueKind::kActual, ValueKind::kSetpoint);
    auto h = o30_comm::make_hand();
    h.hop->put(mi, o30_comm::ramp_slots(), kind == ValueKind::kSetpoint);
    const auto data = h.hand->pid.get(static_cast<PidTerm>(mi), 500ms, kind);
    REQUIRE(data.term == static_cast<PidTerm>(mi));
    REQUIRE(data.kind == kind);
    for (size_t j = 0; j < 20; ++j) {
        REQUIRE(data.values[j] == slots[j]);
    }
    REQUIRE(o30_comm::last_payload(h.transport) ==
            std::vector<uint8_t>{static_cast<uint8_t>(mi | (kind == ValueKind::kSetpoint ? 0x80 : 0)), 0, 36});
    h.hand->pid.set(static_cast<PidTerm>(mi), o30_comm::filled_u8(42), 500ms);
    REQUIRE(h.hop->writes.size() == 3);
    for (const auto &write : h.hop->writes) {
        REQUIRE(write.mi == mi);
        for (const auto v : write.payload) {
            REQUIRE(v == 42);
        }
    }
    h.transport->clear_sent();
    REQUIRE_THROWS_AS(h.hand->pid.get(static_cast<PidTerm>(0x20)), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->pid.set(static_cast<PidTerm>(0x27), {}), linkerbot::hand::ValidationError);
    REQUIRE(h.transport->sent().empty());
}

TEST_CASE("comm: O30 response and snapshot retain the actual receive timestamp", "[comm][o30][runtime]") {
    auto h = o30_comm::make_hand();
    h.hop->put(0x02, o30_comm::ramp_slots());
    const auto data = h.hand->speed.get_blocking(500ms);
    const auto sample = h.hand->runtime()->snapshot("speed/relay_");
    REQUIRE(sample.has_value());
    REQUIRE(data.timestamp == test_support::require_value(sample).received_at);
    REQUIRE(test_support::require_value(h.hand->speed.get_snapshot()).timestamp ==
            test_support::require_value(sample).received_at);
    REQUIRE(h.hand->runtime()->response_provenance().received_at == test_support::require_value(sample).received_at);
}

TEST_CASE("comm: O30 stream handles survive replacement and reject creation after stop",
          "[comm][o30][runtime][lifecycle]") {
    auto h = o30_comm::make_hand();
    auto &legacy = h.hand->stream();
    h.hand->stop_stream();
    auto next = h.hand->stream_handle();
    REQUIRE(legacy.closed());
    REQUIRE_FALSE(next->closed());
    h.hand->stop();
    REQUIRE(next->closed());
    REQUIRE_THROWS(h.hand->stream_handle());
    h.hand.reset();
    REQUIRE(next->closed());
}

TEST_CASE("comm: destroying O30 in an event callback stops facade polling", "[comm][o30][runtime][lifecycle]") {
    auto h = o30_comm::make_hand();
    auto core = h.hand->runtime();
    std::promise<void> polling, destroyed;
    std::atomic<bool> poll_exited{false};
    core->start_polling({{"blocked", 30s, [&](auto cancellation) {
                              polling.set_value();
                              try {
                                  core->wait_until(linkerbot::hand::runtime::Clock::now() + 30s, cancellation);
                              } catch (...) {
                                  poll_exited = true;
                                  throw;
                              }
                          }}});
    polling.get_future().wait();
    h.hop->put(0x02, o30_comm::ramp_slots());
    (void)h.hand->speed.get_blocking(500ms);
    core->subscribe("destroy", [&](const auto &) {
        h.hand.reset();
        destroyed.set_value();
    });
    core->publish("destroy", 1);
    const auto completion = destroyed.get_future().wait_for(1s);
    const bool stopped = poll_exited;
    const auto state = core->runtime_health().state;
    core->close();
    REQUIRE(completion == std::future_status::ready);
    REQUIRE(stopped);
    REQUIRE((state == linkerbot::hand::runtime::DeviceState::closing ||
             state == linkerbot::hand::runtime::DeviceState::closed));
}

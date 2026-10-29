#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/l30/events.hpp>
#include <linkerbot/hand/l30/joints.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::EventQueue;
using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::AngleEvent;
using linkerbot::hand::l30::CurrentEvent;
using linkerbot::hand::l30::FaultEvent;
using linkerbot::hand::l30::kJointCount;
using linkerbot::hand::l30::kJointSpecs;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::SensorEvent;
using linkerbot::hand::l30::SpeedEvent;
using linkerbot::hand::l30::TemperatureEvent;

namespace {
    namespace proto = linkerbot::hand::l30::protocol;

    constexpr auto kTimeout = std::chrono::milliseconds{500};

    bool contains(const std::string &haystack, const std::string &needle) {
        return haystack.find(needle) != std::string::npos;
    }

    // Drains the queue on a worker thread for a bounded window, exactly like the
    // L6 force-sensor comm test: stop_stream() ends the iteration, so the join
    // can never hang.
    std::vector<SensorEvent> collect_events(L30 &hand, std::chrono::milliseconds duration) {
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

    std::unique_ptr<L30> make_hand(FakeTransport **out) {
        auto transport = std::make_unique<FakeTransport>();
        *out = transport.get();
        l30_comm::install_periodic_acks(*out);
        (*out)->on_request(l30_comm::query_id(proto::kSubPosition), l30_comm::i16_handler(l30_comm::filled_i16(450)));
        (*out)->on_request(l30_comm::query_id(proto::kSubTemperature), l30_comm::u8_handler(l30_comm::filled_u8(37)));
        return std::make_unique<L30>(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    }
} // namespace

// FakeTransport：stream() 的生命周期与事件分发——重复 stream 应拒绝，
// 各子系统的周期报告要变成对应的 variant 分支，snapshot 按需填充。

// stream() 未 stop_stream 就再调一次应抛 logic_error，消息含 already active。
TEST_CASE("comm: stream twice without stopping throws", "[comm][l30][streaming]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    static_cast<void>(hand->stream());

    std::string message;
    try {
        static_cast<void>(hand->stream());
    } catch (const linkerbot::hand::LifecycleError &error) {
        message = error.what();
    }
    REQUIRE(contains(message, "already active"));

    hand->stop_stream();
    hand->stop();
}

// stop_stream() 之后可以重新 stream()，拿到的是新队列。
TEST_CASE("comm: stream can be restarted after stop_stream", "[comm][l30][streaming]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    auto *first = &hand->stream();
    hand->stop_stream();
    REQUIRE(first->closed());

    auto *second = &hand->stream();
    REQUIRE_FALSE(second->closed());

    hand->stop_stream();
    hand->stop();
}

// 注入一帧周期角度报告，队列里应出现 AngleEvent，且百分比与 raw 反算一致。
TEST_CASE("comm: a periodic angle report becomes an AngleEvent", "[comm][l30][streaming]") {
    constexpr int16_t kRaw = 450;

    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    auto &queue = hand->stream();
    std::vector<SensorEvent> collected;
    std::thread collector([&] {
        for (const auto &event : queue) {
            collected.push_back(event);
        }
    });
    raw->inject(l30_comm::periodic_i16_frame(proto::kSubPosition, l30_comm::filled_i16(kRaw)));
    std::this_thread::sleep_for(kTimeout);
    hand->stop_stream();
    collector.join();

    REQUIRE_FALSE(collected.empty());
    REQUIRE(std::holds_alternative<AngleEvent>(collected.front()));

    const auto &angles = std::get<AngleEvent>(collected.front()).data.angles;
    for (size_t i = 0; i < kJointCount; ++i) {
        const auto expected = linkerbot::hand::l30::raw_to_percent(kRaw, kJointSpecs[i]);
        REQUIRE(angles.values[i] == Catch::Approx(expected));
    }

    hand->stop();
}

// 角度/速度/电流/温度/故障五路周期报告都应落到各自的 variant 分支上。
TEST_CASE("comm: reports from several subsystems keep their own alternative", "[comm][l30][streaming]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    auto &queue = hand->stream();
    std::vector<SensorEvent> collected;
    std::thread collector([&] {
        for (const auto &event : queue) {
            collected.push_back(event);
        }
    });

    raw->inject(l30_comm::periodic_i16_frame(proto::kSubPosition, l30_comm::filled_i16(450)));
    raw->inject(l30_comm::periodic_i16_frame(proto::kSubSpeed, l30_comm::filled_i16(120)));
    raw->inject(l30_comm::periodic_i16_frame(proto::kSubCurrent, l30_comm::filled_i16(-70)));
    raw->inject(l30_comm::periodic_u8_frame(proto::kSubTemperature, l30_comm::filled_u8(41)));
    raw->inject(l30_comm::periodic_u8_frame(proto::kSubFault, l30_comm::filled_u8(0x04)));

    std::this_thread::sleep_for(kTimeout);
    hand->stop_stream();
    collector.join();

    bool has_angle = false;
    bool has_speed = false;
    bool has_current = false;
    bool has_temperature = false;
    bool has_fault = false;
    for (const auto &event : collected) {
        has_angle = has_angle || std::holds_alternative<AngleEvent>(event);
        has_speed = has_speed || std::holds_alternative<SpeedEvent>(event);
        has_current = has_current || std::holds_alternative<CurrentEvent>(event);
        has_temperature = has_temperature || std::holds_alternative<TemperatureEvent>(event);
        has_fault = has_fault || std::holds_alternative<FaultEvent>(event);
    }
    REQUIRE(has_angle);
    REQUIRE(has_speed);
    REQUIRE(has_current);
    REQUIRE(has_temperature);
    REQUIRE(has_fault);

    hand->stop();
}

// 速度报告在线上是无符号的：120 counts 应原样出现在 SpeedEvent 里。
TEST_CASE("comm: a speed report carries unsigned counts", "[comm][l30][streaming]") {
    constexpr uint16_t kCounts = 120;

    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    auto &queue = hand->stream();
    std::vector<SensorEvent> collected;
    std::thread collector([&] {
        for (const auto &event : queue) {
            collected.push_back(event);
        }
    });
    raw->inject(l30_comm::periodic_i16_frame(proto::kSubSpeed, l30_comm::filled_i16(static_cast<int16_t>(kCounts))));
    std::this_thread::sleep_for(kTimeout);
    hand->stop_stream();
    collector.join();

    REQUIRE_FALSE(collected.empty());
    REQUIRE(std::holds_alternative<SpeedEvent>(collected.front()));
    REQUIRE(std::get<SpeedEvent>(collected.front()).data.speeds[0] == kCounts);

    hand->stop();
}

// 什么都没发生时 snapshot 全是 nullopt。
TEST_CASE("comm: a fresh snapshot holds nothing", "[comm][l30][streaming]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    const auto snapshot = hand->get_snapshot();
    REQUIRE_FALSE(snapshot.angle.has_value());
    REQUIRE_FALSE(snapshot.speed.has_value());
    REQUIRE_FALSE(snapshot.current_limit.has_value());
    REQUIRE_FALSE(snapshot.current.has_value());
    REQUIRE_FALSE(snapshot.temperature.has_value());
    REQUIRE_FALSE(snapshot.fault.has_value());
    REQUIRE_FALSE(snapshot.force_sensor.has_value());

    hand->stop();
}

// 一次阻塞读加一帧周期报告之后，对应的 snapshot 项才有值。
TEST_CASE("comm: reads and reports engage the matching snapshots", "[comm][l30][streaming]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    static_cast<void>(hand->temperature.get_blocking(kTimeout));
    raw->inject(l30_comm::periodic_i16_frame(proto::kSubPosition, l30_comm::filled_i16(450)));

    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    bool angle_ready = false;
    while (std::chrono::steady_clock::now() < deadline && !angle_ready) {
        angle_ready = hand->get_snapshot().angle.has_value();
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }

    const auto snapshot = hand->get_snapshot();
    REQUIRE(snapshot.angle.has_value());
    REQUIRE(snapshot.temperature.has_value());
    REQUIRE(snapshot.temperature->temperatures[0] == 37);
    REQUIRE_FALSE(snapshot.speed.has_value());
    REQUIRE_FALSE(snapshot.fault.has_value());

    hand->stop();
}

// 没有 stream 时 stop_stream() 也安全，重复调也安全。
TEST_CASE("comm: stop_stream is safe without an active stream", "[comm][l30][streaming]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    REQUIRE_NOTHROW(hand->stop_stream());
    static_cast<void>(hand->stream());
    REQUIRE_NOTHROW(hand->stop_stream());
    REQUIRE_NOTHROW(hand->stop_stream());

    hand->stop();
}

// collect_events 这个辅助路径本身也要跑通：报告边发边收，最后能干净 join。
TEST_CASE("comm: collect_events drains the queue and joins", "[comm][l30][streaming]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    std::thread feeder([raw] {
        for (int i = 0; i < 5; ++i) {
            raw->inject(l30_comm::periodic_i16_frame(proto::kSubPosition, l30_comm::filled_i16(450)));
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    });

    const auto events = collect_events(*hand, kTimeout);
    feeder.join();

    REQUIRE_FALSE(events.empty());
    for (const auto &event : events) {
        REQUIRE(std::holds_alternative<AngleEvent>(event));
    }

    hand->stop();
}

#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/o30/events.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

#include "hand/o30/comm_helpers.hpp"

using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::hand::o30::AngleEvent;
using linkerbot::hand::o30::kDefaultPollInterval;
using linkerbot::hand::o30::O30;
using linkerbot::hand::o30::SensorSource;
using linkerbot::hand::o30::protocol::kMiPosition;
using linkerbot::hand::o30::protocol::kMiSpeed;

namespace {
    constexpr auto kTimeout = o30_comm::kTimeout;
    constexpr auto kPollWindow = std::chrono::milliseconds{2000};

    bool wait_until(const std::function<bool()> &pred, std::chrono::milliseconds limit = kPollWindow) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return pred();
    }

    size_t count_mi(FakeTransport *transport, uint8_t mi) {
        size_t n = 0;
        for (const auto &payload : o30_comm::payloads(transport)) {
            if (!payload.empty() && (payload[0] & linkerbot::hand::o30::protocol::kCtrlMiMask) == mi) {
                ++n;
            }
        }
        return n;
    }
} // namespace

// FakeTransport：入口类默认 CAN ID、快照 / 流 / 轮询，以及自有 vs 注入 dispatcher。

// 默认请求 0x001、应答 0x401；写角度走标准 FD 无 BRS。
TEST_CASE("comm: O30 defaults match the measured standard CAN endpoint", "[comm][o30][lifecycle]") {
    auto h = o30_comm::make_hand();
    REQUIRE(h.hand->request_id() == 0x001);
    REQUIRE(h.hand->response_id() == 0x401);
    h.hand->angle.set_angles(o30_comm::filled_u8(0x80), kTimeout);
    for (const auto &msg : h.transport->sent()) {
        REQUIRE(msg.id == 0x001);
        REQUIRE_FALSE(msg.is_extended);
        REQUIRE(msg.is_fd);
        REQUIRE_FALSE(msg.is_brs);
    }
}

// 自定义请求 ID，应答 = 请求 + 0x400。
TEST_CASE("comm: O30 supports a custom standard request ID", "[comm][o30][lifecycle]") {
    auto h = o30_comm::make_hand(0x123);
    REQUIRE(h.hand->request_id() == 0x123);
    REQUIRE(h.hand->response_id() == 0x523);
    h.hand->speed.set_all(1, kTimeout);
    REQUIRE(h.transport->sent().front().id == 0x123);
}

// 构造时不发任何帧；stop 幂等，析构安静。
TEST_CASE("comm: construction is silent and stop is idempotent", "[comm][o30][lifecycle]") {
    auto transport = std::make_unique<FakeTransport>();
    auto hop = std::make_shared<o30_comm::HopEndpoint>();
    hop->seed_defaults();
    o30_comm::attach_hop(transport.get(), hop);
    auto *raw = transport.get();
    {
        O30 hand(std::move(transport));
        REQUIRE(raw->sent().empty());
        REQUIRE_NOTHROW(hand.stop());
        REQUIRE_NOTHROW(hand.stop());
    }
    REQUIRE(true);
}

// 自有 dispatcher：stop 会关掉 transport。
TEST_CASE("comm: stop closes an owned dispatcher", "[comm][o30][lifecycle]") {
    auto h = o30_comm::make_hand();
    REQUIRE(h.transport->close_calls() == 0);
    h.hand->stop();
    REQUIRE(h.transport->close_calls() >= 1);
}

// 注入 dispatcher 不归本手所有：stop 后第二只手还能收发。
TEST_CASE("comm: stop leaves an injected dispatcher running", "[comm][o30][lifecycle]") {
    auto hop_a = std::make_shared<o30_comm::HopEndpoint>();
    hop_a->request_id = 0x001;
    hop_a->seed_defaults();
    hop_a->put(kMiPosition, o30_comm::ramp_slots());
    auto hop_b = std::make_shared<o30_comm::HopEndpoint>();
    hop_b->request_id = 0x002;
    hop_b->seed_defaults();
    hop_b->put(kMiPosition, o30_comm::ramp_slots());

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    o30_comm::attach_hop(raw, hop_a);
    raw->on_request(hop_b->request_id, hop_b->handler());

    CANDispatcher dispatcher(std::move(transport));
    O30 first(dispatcher, 0x001);
    O30 second(dispatcher, 0x002);

    first.stop();
    REQUIRE(raw->close_calls() == 0);

    const auto data = second.angle.get_blocking(kTimeout);
    REQUIRE(data.angles.to_raw() == o30_comm::projected_ramp());

    second.stop();
    REQUIRE(raw->close_calls() == 0);
    dispatcher.stop();
}

// 不存在的 SocketCAN 接口应抛。
TEST_CASE("the interface-name constructor rejects a bogus interface", "[hardware][o30][lifecycle]") {
    REQUIRE_THROWS_AS(O30("o30nosuchif0"), std::exception);
}

// 阻塞读之后 snapshot 与 stream 都能拿到同一份角度。
TEST_CASE("comm: snapshot and stream receive a blocking angle read", "[comm][o30][lifecycle]") {
    auto h = o30_comm::make_hand();
    h.hop->put(kMiPosition, o30_comm::ramp_slots());

    REQUIRE_FALSE(h.hand->get_snapshot().angle.has_value());
    auto &queue = h.hand->stream();
    const auto data = h.hand->angle.get_blocking(kTimeout);
    const auto event = queue.pop();
    const auto snapshot = h.hand->get_snapshot();

    REQUIRE(event.has_value());
    REQUIRE(std::holds_alternative<AngleEvent>(*event));
    REQUIRE(std::get<AngleEvent>(*event).data.angles.to_raw() == data.angles.to_raw());
    REQUIRE(snapshot.angle.has_value());
    REQUIRE(snapshot.angle->angles.to_raw() == data.angles.to_raw());

    h.hand->stop_stream();
}

// 已在 stream 时再调 stream() 应抛 logic_error。
TEST_CASE("comm: stream twice without stopping throws", "[comm][o30][lifecycle]") {
    auto h = o30_comm::make_hand();
    static_cast<void>(h.hand->stream());
    REQUIRE_THROWS_AS(h.hand->stream(), linkerbot::hand::LifecycleError);
    h.hand->stop_stream();
}

// stop_stream 之后可以重新 stream。
TEST_CASE("comm: stream can be restarted after stop_stream", "[comm][o30][lifecycle]") {
    auto h = o30_comm::make_hand();
    auto *first = &h.hand->stream();
    h.hand->stop_stream();
    REQUIRE(first->closed());
    auto *second = &h.hand->stream();
    REQUIRE_FALSE(second->closed());
    h.hand->stop_stream();
}

// start_polling({}) 默认只轮询角度。
TEST_CASE("comm: start_polling defaults to angle only", "[comm][o30][polling]") {
    auto h = o30_comm::make_hand();
    h.hop->put(kMiPosition, o30_comm::ramp_slots());
    h.transport->clear_sent();

    REQUIRE(kDefaultPollInterval == std::chrono::milliseconds{100});
    h.hand->start_polling({});
    const auto polled = wait_until([&] { return count_mi(h.transport, kMiPosition) >= 2; });
    h.hand->stop_polling();

    REQUIRE(polled);
    REQUIRE(count_mi(h.transport, kMiSpeed) == 0);
}

// 非正间隔抛 invalid_argument，文案点名源。
TEST_CASE("comm: a non-positive interval names the offending source", "[comm][o30][polling]") {
    auto h = o30_comm::make_hand();
    std::string message;
    try {
        h.hand->start_polling({{SensorSource::kTemperature, std::chrono::milliseconds{0}}});
    } catch (const linkerbot::hand::PollingPlanError &error) {
        message = error.what();
    }
    REQUIRE(o30_comm::contains(message, "temperature"));
    REQUIRE(o30_comm::contains(message, "must be positive"));
}

// 多源轮询会发出对应 MI，并填 snapshot。
TEST_CASE("comm: polling several sources emits every MI", "[comm][o30][polling]") {
    auto h = o30_comm::make_hand();
    h.hop->put(kMiPosition, o30_comm::ramp_slots());
    h.hop->put(kMiSpeed, o30_comm::ramp_slots());
    h.transport->clear_sent();

    h.hand->start_polling({
        {SensorSource::kAngle, std::chrono::milliseconds{20}},
        {SensorSource::kSpeed, std::chrono::milliseconds{20}},
    });
    const auto seen =
        wait_until([&] { return count_mi(h.transport, kMiPosition) > 0 && count_mi(h.transport, kMiSpeed) > 0; });
    h.hand->stop_polling();

    REQUIRE(seen);
    const auto snapshot = h.hand->get_snapshot();
    REQUIRE(snapshot.angle.has_value());
    REQUIRE(snapshot.speed.has_value());
}

// stop_polling 可重复，停后不再发帧。
TEST_CASE("comm: stop_polling joins the threads and is idempotent", "[comm][o30][polling]") {
    auto h = o30_comm::make_hand();
    h.hop->put(kMiPosition, o30_comm::ramp_slots());
    h.hand->start_polling({{SensorSource::kAngle, std::chrono::milliseconds{20}}});
    REQUIRE(wait_until([&] { return count_mi(h.transport, kMiPosition) > 0; }));

    REQUIRE_NOTHROW(h.hand->stop_polling());
    REQUIRE_NOTHROW(h.hand->stop_polling());
    h.transport->clear_sent();
    std::this_thread::sleep_for(std::chrono::milliseconds{80});
    REQUIRE(h.transport->sent().empty());
}

// closed 之后 manager 再读应失败。
TEST_CASE("comm: stop closes the client used by managers", "[comm][o30][lifecycle]") {
    auto h = o30_comm::make_hand();
    h.hand->stop();
    REQUIRE_THROWS_AS(h.hand->angle.get_blocking(kTimeout), std::runtime_error);
}

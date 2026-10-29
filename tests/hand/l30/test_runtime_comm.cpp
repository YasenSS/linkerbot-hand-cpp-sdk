#include "hand/l30/comm_helpers.hpp"
#include "hand/o6/legacy_regressions.hpp"
#include <catch2/catch_test_macros.hpp>
#include <condition_variable>
#include <future>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol_backend.hpp>
#include <linkerbot/hand/o6/o6.hpp>
using namespace std::chrono_literals;
namespace runtime = linkerbot::hand::runtime;
namespace o6 = linkerbot::hand::o6;
using namespace linkerbot::hand::l30;
using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
namespace {
    struct Gate {
        std::mutex mutex;
        std::condition_variable cv;
        bool entered = false, released = false;
        void block() {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            cv.notify_all();
            cv.wait_for(lock, 3s, [&] { return released; });
        }
        bool wait() {
            std::unique_lock<std::mutex> lock(mutex);
            return cv.wait_for(lock, 1s, [&] { return entered; });
        }
        void release() {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
            cv.notify_all();
        }
    };
    struct EarlyReplyFailure : FakeTransport {
        Gate after_reply;
        std::atomic<bool> fail{false};
        void write(const Message &m) override {
            FakeTransport::write(m);
            if (fail) {
                after_reply.block();
                throw std::runtime_error("backend rejected after early reply");
            }
        }
    };
    template <class Fn> bool fails(Fn fn) {
        try {
            fn();
            return false;
        } catch (...) {
            return true;
        }
    }
    template <class Fn> bool eventually(Fn fn) {
        const auto end = std::chrono::steady_clock::now() + 1s;
        while (std::chrono::steady_clock::now() < end) {
            if (fn()) {
                return true;
            }
            std::this_thread::yield();
        }
        return fn();
    }
}
TEST_CASE("comm: L30 shared Session Runtime close leaves a different model usable", "[comm][l30][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    std::promise<void> sent;
    raw->on_request(l30_comm::query_id(protocol::kSubPosition), [&](const Message &) {
        sent.set_value();
        return std::vector<Message>{};
    });
    raw->on_request(0x28, [](const Message &m) {
        auto reply = m;
        reply.dlc = 7;
        return std::vector<Message>{reply};
    });
    auto session = linkerbot::transport::Session::own(std::move(fake));
    L30 hand(session, 1, 0, false);
    o6::O6 other(o6::Side::kLeft, session, "fake", test_util::unique_mapping_path());
    auto waiting = std::async(std::launch::async, [&] { return fails([&] { (void)hand.angle.get_blocking(5s); }); });
    const auto started = sent.get_future().wait_for(1s);
    // An ACK/read wait does not hold the physical send lock.
    const bool usable_before_close = !fails([&] { (void)other.speed.get_blocking(500ms); });
    hand.stop();
    REQUIRE(started == std::future_status::ready);
    REQUIRE(usable_before_close);
    REQUIRE(waiting.get());
    REQUIRE(hand.runtime_health().in_flight == 0);
    REQUIRE(hand.runtime_health().state == runtime::DeviceState::closed);
    REQUIRE(raw->close_calls() == 0);
    REQUIRE_NOTHROW(other.speed.get_blocking(500ms));
}
TEST_CASE("comm: L30 standalone Client manager shares runtime and removes timed out transactions",
          "[comm][l30][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    linkerbot::can::CANDispatcher dispatcher(std::move(fake));
    Client client(dispatcher, 1, 0);
    AngleManager angle(client);
    const auto core = client.runtime();
    REQUIRE_THROWS(angle.get_blocking(10ms));
    REQUIRE(core->runtime_health().in_flight == 0);
    REQUIRE(core->runtime_health().timeouts == 1);
    const auto count = raw->sent().size();
    REQUIRE_THROWS_AS(angle.get_blocking(100ms), Client::QuarantinedError);
    REQUIRE(raw->sent().size() == count);
    REQUIRE(core->pending_count(protocol::response_key(l30_comm::response_id(protocol::kParentQuery, 1, 0))) == 0);
}
TEST_CASE("comm: L30 an early full response cannot hide a failed backend send or update typed cache",
          "[comm][l30][runtime-migration]") {
    auto fake = std::make_unique<EarlyReplyFailure>();
    auto *raw = fake.get();
    raw->on_request(l30_comm::query_id(1), l30_comm::i16_handler(l30_comm::filled_i16(100)));
    L30 hand(std::move(fake), 1, 0, false);
    const auto old = hand.angle.get_blocking(500ms);
    raw->on_request(l30_comm::query_id(1), l30_comm::i16_handler(l30_comm::filled_i16(450)));
    raw->fail = true;
    auto read = std::async(std::launch::async, [&] { return fails([&] { (void)hand.angle.get_blocking(2s); }); });
    const bool entered = raw->after_reply.wait();
    const auto pending = read.wait_for(20ms);
    const auto during = hand.angle.get_snapshot();
    raw->after_reply.release();
    REQUIRE(entered);
    REQUIRE(pending == std::future_status::timeout);
    REQUIRE(read.get());
    REQUIRE(during->timestamp == old.timestamp);
    REQUIRE(hand.angle.get_snapshot()->timestamp == old.timestamp);
    REQUIRE(hand.runtime_health().in_flight == 0);
}
TEST_CASE("comm: L30 partial five-finger failure preserves only the previous complete cache",
          "[comm][l30][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    for (uint8_t sub = 1; sub <= 5; ++sub) {
        raw->on_request(l30_comm::request_id(2, sub, 0), l30_comm::tactile_handler(l30_comm::tactile_ramp()));
    }
    L30 hand(std::move(fake), 1, 0, false);
    const auto previous = hand.force_sensor.get_blocking(500ms);
    raw->on_request(l30_comm::request_id(2, 5, 0), [](const Message &m) {
        auto frames = l30_comm::tactile_handler(l30_comm::tactile_ramp())(m);
        frames.pop_back();
        return frames;
    });
    REQUIRE_THROWS(hand.force_sensor.get_blocking(40ms));
    REQUIRE(hand.force_sensor.get_snapshot()->timestamp == previous.timestamp);
    REQUIRE(hand.runtime_health().in_flight == 0);
}
TEST_CASE("comm: L30 report event worker cannot block RX or synchronous request completion",
          "[comm][l30][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    raw->on_request(l30_comm::query_id(1), l30_comm::i16_handler(l30_comm::filled_i16(450)));
    linkerbot::can::CANDispatcher dispatcher(std::move(fake));
    Client client(dispatcher, 1, 0);
    Gate gate;
    std::atomic<bool> rx{true};
    const auto id = client.add_report_handler(1, [&](const auto &) {
        rx = linkerbot::can::CANDispatcher::is_any_receive_thread();
        gate.block();
    });
    raw->inject(l30_comm::periodic_i16_frame(1, l30_comm::filled_i16(100)));
    const bool entered = gate.wait();
    const bool completed = !fails([&] { (void)client.read(5, 1, 500ms); });
    auto removing = std::async(std::launch::async, [&] { client.remove_report_handler(1, id); });
    const auto barrier = removing.wait_for(20ms);
    gate.release();
    removing.get();
    REQUIRE(entered);
    REQUIRE_FALSE(rx);
    REQUIRE(completed);
    REQUIRE(barrier == std::future_status::timeout);
}
TEST_CASE("comm: L30 polling cancellation is scoped and runtime owns worker restart",
          "[comm][l30][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    std::promise<void> tactile_sent, angle_sent;
    raw->on_request(l30_comm::request_id(2, 1, 0), [&](const Message &) {
        tactile_sent.set_value();
        return std::vector<Message>{};
    });
    raw->on_request(l30_comm::query_id(1), [&](const Message &) {
        angle_sent.set_value();
        return std::vector<Message>{};
    });
    L30 hand(std::move(fake), 1, 0, false);
    auto application = std::async(
        std::launch::async, [&] { return fails([&] { (void)hand.force_sensor.get_finger(Finger::kThumb, 2s); }); });
    const auto application_started = tactile_sent.get_future().wait_for(1s);
    hand.start_polling({{SensorSource::kAngle, 5s}});
    const auto poll_started = angle_sent.get_future().wait_for(1s);
    hand.stop_polling();
    const auto application_still_waiting = application.wait_for(20ms);
    for (const auto &m : l30_comm::tactile_frames(l30_comm::tactile_ramp(), l30_comm::response_id(2, 1, 0))) {
        raw->inject(m);
    }
    REQUIRE(application_started == std::future_status::ready);
    REQUIRE(poll_started == std::future_status::ready);
    REQUIRE(application_still_waiting == std::future_status::timeout);
    REQUIRE_FALSE(application.get());
    REQUIRE(hand.runtime_health().in_flight == 0);
    REQUIRE(hand.runtime_health().state == runtime::DeviceState::ready);
}
TEST_CASE("comm: L30 terminal tactile device error permits next transaction", "[comm][l30][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    raw->on_request(l30_comm::request_id(2, 1, 0), l30_comm::ack_handler(0x21));
    L30 hand(std::move(fake), 1, 0, false);
    REQUIRE_THROWS_AS(hand.force_sensor.get_finger(Finger::kThumb, 500ms), protocol::ProtocolError);
    REQUIRE(hand.runtime_health().in_flight == 0);
    raw->on_request(l30_comm::request_id(2, 1, 0), l30_comm::tactile_handler(l30_comm::tactile_ramp()));
    REQUIRE_NOTHROW(hand.force_sensor.get_finger(Finger::kThumb, 500ms));
}
TEST_CASE("comm: L30 Runtime constructor binds the selected endpoint and rejects mismatches",
          "[comm][l30][runtime-migration]") {
    auto session = linkerbot::transport::Session::own(std::make_unique<FakeTransport>());
    Client client(session, 7, 3);
    auto core = client.runtime();
    REQUIRE_THROWS_AS(L30(core, 8, 3, false), linkerbot::hand::ValidationError);
    L30 hand(core, 7, 3, false);
    REQUIRE(hand.runtime() == core);
    REQUIRE(hand.node_id() == 7);
    REQUIRE(hand.host_id() == 3);
}
TEST_CASE("comm: L30 compatibility thread cancellation cannot revive an active request",
          "[comm][l30][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    std::promise<void> sent;
    raw->on_request(l30_comm::query_id(1), [&](const Message &) {
        sent.set_value();
        return std::vector<Message>{};
    });
    linkerbot::can::CANDispatcher dispatcher(std::move(fake));
    Client client(dispatcher, 1, 0);
    std::promise<std::thread::id> owner;
    auto read = std::async(std::launch::async, [&] {
        owner.set_value(std::this_thread::get_id());
        return fails([&] { (void)client.read(5, 1, 5s); });
    });
    const auto thread = owner.get_future().get();
    const auto started = sent.get_future().wait_for(1s);
    client.cancel_thread(thread);
    client.resume_thread(thread);
    const auto cancelled = read.wait_for(200ms);
    if (cancelled != std::future_status::ready) {
        client.close();
    }
    REQUIRE(started == std::future_status::ready);
    REQUIRE(cancelled == std::future_status::ready);
    REQUIRE(read.get());
    REQUIRE(client.runtime()->runtime_health().in_flight == 0);
    REQUIRE_THROWS_AS(client.read(5, 1, 100ms), Client::QuarantinedError);
}
TEST_CASE("comm: L30 facade destroyed on Runtime event worker drains polling and disables captured sinks",
          "[comm][l30][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    raw->on_request(l30_comm::query_id(1), l30_comm::i16_handler(l30_comm::filled_i16(100)));
    auto session = linkerbot::transport::Session::own(std::move(fake));
    auto hand = std::make_unique<L30>(session, 1, 0, false);
    auto core = hand->runtime();
    std::promise<void> destroyed;
    const auto id = core->subscribe("l30.angle", [&](const auto &) {
        hand.reset();
        destroyed.set_value();
    });
    hand->start_polling({{SensorSource::kAngle, 5s}});
    const auto status = destroyed.get_future().wait_for(2s);
    core->unsubscribe(id);
    core->close();
    REQUIRE(status == std::future_status::ready);
    REQUIRE_FALSE(hand);
    REQUIRE(core->runtime_health().in_flight == 0);
    REQUIRE(core->runtime_health().state == runtime::DeviceState::closed);
}
TEST_CASE("comm: L30 nested public operations keep their actual parent deadline", "[comm][l30][runtime-migration]") {
    auto session = linkerbot::transport::Session::own(std::make_unique<FakeTransport>());
    Client client(session, 1, 0);
    auto outer = client.operation(200ms);
    auto inner = client.operation(5s, true);
    REQUIRE(inner.deadline() == outer.deadline());
    REQUIRE(inner.remaining() <= 200ms);
}
TEST_CASE("comm: L30 a queued old report cannot overwrite a newer blocking read", "[comm][l30][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    raw->on_request(l30_comm::query_id(1), l30_comm::i16_handler(l30_comm::filled_i16(450)));
    linkerbot::can::CANDispatcher dispatcher(std::move(fake));
    Client client(dispatcher, 1, 0);
    Gate gate;
    const auto blocker = client.add_report_handler(1, [&](const auto &) { gate.block(); });
    AngleManager manager(client);
    raw->inject(l30_comm::periodic_i16_frame(1, l30_comm::filled_i16(100)));
    const bool entered = gate.wait();
    const auto latest = manager.get_blocking(500ms);
    std::promise<void> report_processed;
    const auto fence = client.add_report_handler(2, [&](const auto &) { report_processed.set_value(); });
    raw->inject(l30_comm::periodic_i16_frame(2, l30_comm::filled_i16(100)));
    gate.release();
    const auto processed = report_processed.get_future().wait_for(1s);
    client.remove_report_handler(1, blocker);
    client.remove_report_handler(2, fence);
    REQUIRE(entered);
    REQUIRE(processed == std::future_status::ready);
    REQUIRE(manager.get_snapshot()->timestamp == latest.timestamp);
    REQUIRE(manager.get_snapshot()->angles.values == latest.angles.values);
}

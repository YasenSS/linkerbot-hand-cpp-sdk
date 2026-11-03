#include "hand/l30/comm_helpers.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <condition_variable>
#include <future>
#include <linkerbot/hand/l30/l30.hpp>
#include <mutex>

using namespace std::chrono_literals;
using namespace linkerbot::hand::l30;
using linkerbot::can::CANDispatcher;
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
            cv.wait_for(lock, 2s, [&] { return released; });
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
    std::string failure(const std::function<void()> &fn) {
        try {
            fn();
        } catch (const std::exception &e) {
            return e.what();
        }
        return {};
    }
    bool sent(FakeTransport &raw, size_t count) {
        const auto until = std::chrono::steady_clock::now() + 1s;
        while (std::chrono::steady_clock::now() < until) {
            if (raw.sent().size() >= count) {
                return true;
            }
            std::this_thread::yield();
        }
        return false;
    }
    const auto query = l30_comm::query_id(protocol::kSubPosition);
    const auto tactile =
        l30_comm::request_id(protocol::kParentTactile, protocol::kSubTactileThumb, protocol::kAccessRead);
}

TEST_CASE("comm: L30 copied report cannot access a destroyed non-last manager", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 1, 0);
    Gate gate;
    const auto first = client.add_report_handler(protocol::kSubPosition, [&](const auto &) { gate.block(); });
    auto manager = std::make_unique<AngleManager>(client);
    std::atomic<int> calls{0};
    const auto copied = client.add_report_handler(protocol::kSubPosition, [&](const auto &) { ++calls; });
    std::promise<void> last_called;
    auto last_done = last_called.get_future();
    const auto last = client.add_report_handler(protocol::kSubPosition, [&](const auto &) { last_called.set_value(); });
    raw->inject(l30_comm::periodic_i16_frame(protocol::kSubPosition, l30_comm::filled_i16(100)));
    const bool entered = gate.wait();
    manager.reset(); // The dispatch snapshot already contains this manager.
    client.remove_report_handler(protocol::kSubPosition, copied);
    gate.release();
    client.remove_report_handler(protocol::kSubPosition, first);
    const auto completed = last_done.wait_for(1s);
    dispatcher.stop();
    REQUIRE(entered);
    REQUIRE(completed == std::future_status::ready);
    REQUIRE(calls.load() == 0);
    client.remove_report_handler(protocol::kSubPosition, last);
}

TEST_CASE("comm: L30 self cancellation and concurrent removers cross the running barrier", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 1, 0);
    Gate gate;
    Client::HandlerId id = 0;
    id = client.add_report_handler(protocol::kSubPosition, [&](const auto &) {
        client.remove_report_handler(protocol::kSubPosition, id);
        gate.block();
    });
    raw->inject(l30_comm::periodic_i16_frame(protocol::kSubPosition, l30_comm::filled_i16(100)));
    const bool entered = gate.wait();
    auto a = std::async(std::launch::async, [&] { client.remove_report_handler(protocol::kSubPosition, id); });
    auto b = std::async(std::launch::async, [&] { client.remove_report_handler(protocol::kSubPosition, id); });
    const auto aw = a.wait_for(20ms), bw = b.wait_for(20ms);
    gate.release();
    a.get();
    b.get();
    REQUIRE(entered);
    REQUIRE(aw == std::future_status::timeout);
    REQUIRE(bw == std::future_status::timeout);
}

TEST_CASE("comm: L30 rejects malformed responses and reports before dispatch", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(query, [](const Message &req) {
        const auto valid = l30_comm::fd_reply(protocol::expected_response_id(req.id), {1, 0, 0, 42});
        auto classic = valid, standard = valid, oversized = valid, short_frame = valid, marker = valid, length = valid;
        classic.is_fd = false;
        standard.is_extended = false;
        oversized.dlc = 65;
        short_frame.dlc = 2;
        marker.data[1] = 0x10;
        length.data[0] = 62;
        classic.data[3] = standard.data[3] = oversized.data[3] = marker.data[3] = length.data[3] = 7;
        return std::vector<Message>{classic, standard, oversized, short_frame, marker, length, valid};
    });
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 1, 0);
    REQUIRE(client.read(protocol::kParentQuery, protocol::kSubPosition, 500ms).at(3) == 42);
    std::atomic<int> reports{0};
    auto id = client.add_report_handler(protocol::kSubPosition, [&](const auto &) { ++reports; });
    auto good = l30_comm::periodic_i16_frame(protocol::kSubPosition, l30_comm::filled_i16(100));
    auto bad = good;
    bad.data[1] = 0x10;
    raw->inject(bad);
    bad = good;
    bad.is_fd = false;
    raw->inject(bad);
    bad = good;
    bad.dlc = 65;
    raw->inject(bad);
    bad = good;
    bad.data[0] = 63;
    raw->inject(bad);
    // A trailing dispatcher subscription is a deterministic receive fence.
    std::promise<void> fence;
    auto done = fence.get_future();
    auto sub = dispatcher.subscribe([&](const Message &m) {
        if (m.id == 123) {
            fence.set_value();
        }
    });
    Message sentinel;
    sentinel.id = 123;
    raw->inject(sentinel);
    REQUIRE(done.wait_for(1s) == std::future_status::ready);
    client.remove_report_handler(protocol::kSubPosition, id);
    dispatcher.unsubscribe(sub);
    REQUIRE(reports == 0);
}

TEST_CASE("comm: L30 timed out IDs remain quarantined for ordinary and tactile requests", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 1, 0);
    REQUIRE_THROWS(client.read(protocol::kParentQuery, protocol::kSubPosition, 10ms));
    raw->inject(l30_comm::fd_reply(protocol::expected_response_id(query), {1, 0, 0, 99}));
    REQUIRE_THROWS_WITH(client.read(protocol::kParentQuery, protocol::kSubPosition, 100ms),
                        Catch::Matchers::ContainsSubstring("quarantined"));
    REQUIRE_THROWS(client.read_tactile(protocol::kSubTactileThumb, 10ms));
    for (const auto &frame :
         l30_comm::tactile_frames(l30_comm::tactile_ramp(), protocol::expected_response_id(tactile))) {
        raw->inject(frame);
    }
    REQUIRE_THROWS_WITH(client.read_tactile(protocol::kSubTactileThumb, 100ms),
                        Catch::Matchers::ContainsSubstring("quarantined"));
    REQUIRE(raw->sent().size() == 2);
}

TEST_CASE("comm: L30 admission deadline expires without sending and close cancels both lanes", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 1, 0);
    auto a = std::async(std::launch::async, [&] {
        return failure([&] { (void)client.read(protocol::kParentQuery, protocol::kSubPosition, 5s); });
    });
    const bool first = sent(*raw, 1);
    const auto begin = std::chrono::steady_clock::now();
    const auto blocked = failure([&] { (void)client.read(protocol::kParentQuery, protocol::kSubSpeed, 30ms); });
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    auto b = std::async(std::launch::async,
                        [&] { return failure([&] { (void)client.read_tactile(protocol::kSubTactileThumb, 5s); }); });
    const bool second = sent(*raw, 2);
    auto c = std::async(std::launch::async, [&] {
        return failure([&] { (void)client.read(protocol::kParentQuery, protocol::kSubSpeed, 5s); });
    });
    client.close();
    const auto ar = a.wait_for(200ms), br = b.wait_for(200ms), cr = c.wait_for(200ms);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE_FALSE(blocked.empty());
    REQUIRE(elapsed < 200ms);
    REQUIRE(ar == std::future_status::ready);
    REQUIRE(br == std::future_status::ready);
    REQUIRE(cr == std::future_status::ready);
    REQUIRE_FALSE(a.get().empty());
    REQUIRE_FALSE(b.get().empty());
    REQUIRE_FALSE(c.get().empty());
    REQUIRE(raw->sent().size() == 2);
}

TEST_CASE("comm: L30 blocked send obeys the API budget without reporting early success", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    Gate gate;
    transport->on_request(query, [&](const Message &req) {
        gate.block();
        return std::vector<Message>{l30_comm::fd_reply(protocol::expected_response_id(req.id), {0, 0, 0})};
    });
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 1, 0);
    SECTION("timeout cancels a claimed write without waiting for backend return") {
        auto read = std::async(std::launch::async, [&] {
            return failure([&] { (void)client.read(protocol::kParentQuery, protocol::kSubPosition, 30ms); });
        });
        const bool entered = gate.wait();
        const auto status = read.wait_for(150ms);
        gate.release();
        REQUIRE(entered);
        REQUIRE(status == std::future_status::ready);
        REQUIRE_FALSE(read.get().empty());
        REQUIRE_THROWS_WITH(client.read(protocol::kParentQuery, protocol::kSubPosition, 200ms),
                            Catch::Matchers::ContainsSubstring("quarantined"));
    }
    SECTION("success still requires backend completion") {
        auto read = std::async(std::launch::async, [&] {
            return failure([&] { (void)client.read(protocol::kParentQuery, protocol::kSubPosition, 1s); });
        });
        const bool entered = gate.wait();
        const auto status = read.wait_for(30ms);
        gate.release();
        REQUIRE(entered);
        REQUIRE(status == std::future_status::timeout);
        REQUIRE(read.get().empty());
    }
}

TEST_CASE("comm: L30 cancels unclaimed sends and close releases a blocked sender", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    Gate gate;
    transport->on_request(query, [&](const Message &) {
        gate.block();
        return std::vector<Message>{};
    });
    CANDispatcher dispatcher(std::move(transport));
    Client first(dispatcher, 1, 0), second(dispatcher, 2, 0);
    auto read = std::async(std::launch::async, [&] {
        return failure([&] { (void)first.read(protocol::kParentQuery, protocol::kSubPosition, 5s); });
    });
    const bool entered = gate.wait();
    const auto timeout = failure([&] { (void)second.read(protocol::kParentQuery, protocol::kSubPosition, 25ms); });
    auto closing = std::async(std::launch::async, [&] { first.close(); });
    const auto status = read.wait_for(150ms);
    const auto draining = closing.wait_for(20ms);
    gate.release();
    closing.get();
    REQUIRE(draining == std::future_status::timeout);
    const auto error = read.get();
    REQUIRE(entered);
    REQUIRE_FALSE(timeout.empty());
    REQUIRE_FALSE(error.empty());
    REQUIRE(status == std::future_status::ready);
    // Draining the persistent Session proves the queued second request cannot
    // be sent after its caller has already timed out.
    auto session = linkerbot::transport::Session::borrow(dispatcher);
    session->close();
    REQUIRE(raw->sent().size() == 1);
}

TEST_CASE("comm: L30 exclusive Client operation excludes the independent tactile lane", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(0x0220E100, l30_comm::ack_handler());
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 1, 0);
    ControlManager control(client);
    ForceSensorManager force_sensor(client);
    auto transaction = std::async(std::launch::async, [&] {
        return failure([&] {
            const auto operation = client.operation(1s);
            control.disable(operation.remaining());
            control.enable(operation.remaining());
        });
    });
    const bool started = sent(*raw, 1);
    const auto blocked = failure([&] { (void)force_sensor.get_finger(Finger::kThumb, 25ms); });
    raw->inject(l30_comm::fd_reply(protocol::expected_response_id(0x02210100), {0, 0, 0}));
    const auto result = transaction.get();
    REQUIRE(started);
    REQUIRE_FALSE(blocked.empty());
    REQUIRE(result.empty());
    REQUIRE(l30_comm::sent_ids(raw) == std::vector<uint32_t>{0x02210100, 0x0220E100});
}

TEST_CASE("comm: L30 exclusive Client operation excludes competing commands and shares one deadline",
          "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    Gate gate;
    raw->on_request(0x02210100, [&](const Message &req) {
        gate.block();
        return l30_comm::ack_handler()(req);
    });
    raw->on_request(0x0220E100, l30_comm::ack_handler());
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 1, 0);
    ControlManager control(client);
    const auto sequence = [&](std::chrono::milliseconds timeout) {
        const auto operation = client.operation(timeout);
        control.disable(operation.remaining());
        control.enable(operation.remaining());
        control.disable(operation.remaining());
    };
    auto transaction = std::async(std::launch::async, [&] { return failure([&] { sequence(1s); }); });
    const bool entered = gate.wait();
    auto competing = std::async(std::launch::async, [&] { return failure([&] { control.enable(25ms); }); });
    const auto status = competing.wait_for(150ms);
    gate.release();
    const auto competing_error = competing.get(), transaction_error = transaction.get();
    REQUIRE(entered);
    REQUIRE(status == std::future_status::ready);
    REQUIRE_FALSE(competing_error.empty());
    REQUIRE(transaction_error.empty());
    REQUIRE(l30_comm::sent_ids(raw) == std::vector<uint32_t>{0x02210100, 0x0220E100, 0x02210100});
    // Two 30ms ACK delays exceed the shared 50ms budget. Scheduling may
    // exhaust that budget during the first ACK, so either prefix is valid.
    raw->clear_sent();
    for (auto id : {0x02210100u, 0x0220E100u}) {
        raw->on_request(id, [](const Message &req) {
            std::this_thread::sleep_for(30ms);
            return l30_comm::ack_handler()(req);
        });
    }
    REQUIRE_THROWS(sequence(50ms));
    const auto sent = l30_comm::sent_ids(raw);
    REQUIRE_FALSE(sent.empty());
    REQUIRE(sent.size() <= 2);
    auto expected = std::vector<uint32_t>{0x02210100, 0x0220E100};
    expected.resize(sent.size());
    REQUIRE(sent == expected);
}

TEST_CASE("comm: L30 five finger reads consume one budget", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    for (auto finger : {Finger::kThumb, Finger::kIndex, Finger::kMiddle, Finger::kRing, Finger::kPinky}) {
        raw->on_request(
            l30_comm::request_id(protocol::kParentTactile, finger_subcommand(finger), protocol::kAccessRead),
            [](const Message &req) {
                std::this_thread::sleep_for(30ms);
                return l30_comm::tactile_handler(l30_comm::tactile_ramp())(req);
            });
    }
    L30 hand(std::move(transport), 1, 0, false);
    REQUIRE_THROWS(hand.force_sensor.get_blocking(50ms));
    REQUIRE(raw->sent().size() == 2);
    REQUIRE_FALSE(hand.force_sensor.get_snapshot().has_value());
}

TEST_CASE("comm: L30 stop cancels polling waits and interrupts long intervals", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    L30 hand(std::move(transport), 1, 0, false);
    auto waiting = std::async(std::launch::async, [&] { return failure([&] { (void)hand.angle.get_blocking(5s); }); });
    const bool entered = sent(*raw, 1);
    hand.start_polling({{SensorSource::kForceSensor, 5s}});
    const bool polling = sent(*raw, 2);
    const auto start = std::chrono::steady_clock::now();
    hand.stop();
    REQUIRE(std::chrono::steady_clock::now() - start < 300ms);
    REQUIRE(entered);
    REQUIRE(polling);
    REQUIRE_FALSE(waiting.get().empty());
    REQUIRE_THROWS(hand.start_polling());
}

TEST_CASE("comm: L30 tactile conflicting duplicate and error ACK are terminal", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(tactile, [](const Message &req) {
        auto frames = l30_comm::tactile_handler(l30_comm::tactile_ramp())(req);
        auto conflict = frames[0];
        conflict.data[3] ^= 1;
        return std::vector<Message>{frames[0], conflict, frames[1]};
    });
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 1, 0);
    REQUIRE_THROWS_WITH(client.read_tactile(protocol::kSubTactileThumb, 500ms),
                        Catch::Matchers::ContainsSubstring("conflicting duplicate"));
    const auto index =
        l30_comm::request_id(protocol::kParentTactile, protocol::kSubTactileIndex, protocol::kAccessRead);
    raw->on_request(index, [](const Message &req) {
        auto frames = l30_comm::tactile_handler(l30_comm::tactile_ramp())(req);
        frames.insert(frames.begin(), l30_comm::ack_handler(0x22)(req).front());
        return frames;
    });
    int status = 0;
    try {
        (void)client.read_tactile(protocol::kSubTactileIndex, 500ms);
    } catch (const protocol::ProtocolError &e) {
        status = e.status();
    }
    REQUIRE(status == 0x22);
}

TEST_CASE("comm: L30 stop_polling wakes a long interval and permits restart", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(query, l30_comm::i16_handler(l30_comm::filled_i16(100)));
    L30 hand(std::move(transport), 1, 0, false);
    hand.start_polling({{SensorSource::kAngle, 5s}});
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!hand.angle.get_snapshot() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool received = hand.angle.get_snapshot().has_value();
    const auto begin = std::chrono::steady_clock::now();
    hand.stop_polling();
    REQUIRE(received);
    REQUIRE(std::chrono::steady_clock::now() - begin < 200ms);
    hand.start_polling({{SensorSource::kAngle, 5s}});
    const bool restarted = sent(*raw, 2);
    hand.stop_polling();
    REQUIRE(restarted);
}

TEST_CASE("comm: L30 identity verification shares its read budget", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    const std::vector<uint8_t> info{0x13, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 'B', 'A'};
    transport->on_request(
        l30_comm::request_id(protocol::kParentConfig, protocol::kSubDeviceInfo, protocol::kAccessRead),
        [&](const Message &req) {
            std::this_thread::sleep_for(30ms);
            return l30_comm::payload_handler(info)(req);
        });
    const std::string product = "LHT30-1-1-L-B-3-A";
    transport->on_request(
        l30_comm::request_id(protocol::kParentConfig, protocol::kSubProductCode, protocol::kAccessRead),
        [&](const Message &req) {
            std::this_thread::sleep_for(30ms);
            return l30_comm::payload_handler(std::vector<uint8_t>(product.begin(), product.end()))(req);
        });
    L30 hand(std::move(transport), 1, 0, false);
    REQUIRE_THROWS(hand.version.get_verified_device_info(50ms));
}

TEST_CASE("comm: L30 disabling multiple report sources shares its ACK budget", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l30_comm::install_periodic_acks(raw);
    L30 hand(std::move(transport), 1, 0, false);
    hand.report.configure(SensorSource::kAngle);
    hand.report.configure(SensorSource::kSpeed);
    hand.report.configure(SensorSource::kCurrent);
    raw->clear_sent();
    for (auto sub : {protocol::kSubPosition, protocol::kSubSpeed, protocol::kSubCurrent}) {
        raw->on_request(l30_comm::request_id(protocol::kParentPeriodic, sub, protocol::kAccessWrite),
                        [](const Message &req) {
                            std::this_thread::sleep_for(30ms);
                            return l30_comm::ack_handler()(req);
                        });
    }
    REQUIRE_THROWS(hand.report.disable_all(50ms));
    REQUIRE(raw->sent().size() == 2);
}

TEST_CASE("comm: L30 retired stream references survive replacement and stop is terminal", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    L30 hand(std::move(transport), 1, 0, false);
    auto &old = hand.stream();
    auto consumer = std::async(std::launch::async, [&] { return old.pop(); });
    hand.stop_stream();
    auto &next = hand.stream();
    REQUIRE(&old != &next);
    REQUIRE(old.closed());
    REQUIRE_FALSE(old.pop().has_value());
    REQUIRE_FALSE(consumer.get().has_value());
    hand.stop();
    REQUIRE(next.closed());
    REQUIRE_THROWS(hand.stream());
}

TEST_CASE("comm: L30 explicit stop retries a failed physical close", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->fail_next_closes(1);
    L30 hand(std::move(transport), 1, 0, false);
    REQUIRE_THROWS(hand.stop());
    REQUIRE(raw->close_calls() == 1);
    REQUIRE_NOTHROW(hand.stop());
    REQUIRE(raw->close_calls() == 2);
    REQUIRE_NOTHROW(hand.stop());
    REQUIRE(raw->close_calls() == 2);
}

TEST_CASE("comm: L30 external close drains operation owners after cancelling queued reads", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 1, 0);
    auto operation = std::unique_ptr<Client::Operation>(new Client::Operation(client.operation(5s)));
    auto queued = std::async(std::launch::async, [&] {
        return failure([&] { (void)client.read(protocol::kParentQuery, protocol::kSubPosition, 5s); });
    });
    std::promise<void> closing;
    auto closed = std::async(std::launch::async, [&] {
        closing.set_value();
        client.close();
    });
    closing.get_future().wait();
    const auto cancelled = queued.wait_for(300ms);
    const auto held = closed.wait_for(30ms);
    operation.reset();
    const auto drained = closed.wait_for(300ms);
    closed.get();
    REQUIRE(cancelled == std::future_status::ready);
    REQUIRE_FALSE(queued.get().empty());
    REQUIRE(held == std::future_status::timeout);
    REQUIRE(drained == std::future_status::ready);
}

TEST_CASE("comm: L30 client destruction cancels a sent request before destroying waiters", "[comm][l30][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    auto client = std::make_unique<Client>(dispatcher, 1, 0);
    auto *target = client.get();
    auto request = std::async(std::launch::async, [&] {
        return failure([&] { (void)target->read(protocol::kParentQuery, protocol::kSubPosition, 5s); });
    });
    const bool active = sent(*raw, 1);
    client.reset();
    REQUIRE(active);
    REQUIRE(request.wait_for(300ms) == std::future_status::ready);
    REQUIRE_FALSE(request.get().empty());
}

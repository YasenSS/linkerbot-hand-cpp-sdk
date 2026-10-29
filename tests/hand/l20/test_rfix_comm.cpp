#include "hand/l20/comm_helpers.hpp"
#include "hand/o6/followup_regressions.hpp"
#include "hand/support/temp_path.hpp"
#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <future>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20/l20.hpp>
#include <linkerbot/hand/l20/multi_frame.hpp>
#include <set>
#include <thread>

using namespace std::chrono_literals;
using namespace linkerbot::hand::l20;
using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::can::Message;

namespace {
    constexpr auto id = l20_comm::kLeftId;
    std::vector<Message> joints(uint8_t first = 0x41, uint8_t fill = 30) {
        std::vector<Message> result;
        for (uint8_t i = 0; i < 5; ++i) {
            Message m;
            m.id = id;
            m.dlc = 7;
            m.data[0] = first + i;
            std::fill_n(m.data.begin() + 1, 6, fill);
            result.push_back(m);
        }
        return result;
    }
    std::vector<Message> tactile(uint8_t cmd = 0xB1) {
        std::vector<Message> result;
        for (uint8_t i = 0; i < 12; ++i) {
            Message m;
            m.id = id;
            m.dlc = 8;
            m.data[0] = cmd;
            m.data[1] = i << 4;
            std::fill_n(m.data.begin() + 2, 6, i + 1);
            result.push_back(m);
        }
        return result;
    }
    void drain(FakeTransport &raw, CANDispatcher &dispatcher) {
        std::promise<void> done;
        const auto sub = dispatcher.subscribe([&](const Message &m) {
            if (m.id == 0x701) {
                done.set_value();
            }
        });
        Message sentinel;
        sentinel.id = 0x701;
        raw.inject(sentinel);
        const auto ready = done.get_future().wait_for(1s);
        dispatcher.unsubscribe(sub);
        REQUIRE(ready == std::future_status::ready);
    }
    template <class F> bool fails(F f) {
        try {
            f();
            return false;
        } catch (const std::exception &) {
            return true;
        }
    }
}

TEST_CASE("comm: l20 unsolicited and malformed joint replies cannot publish", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    AngleManager manager(id, dispatcher, test_util::unique_mapping_path());
    manager.start();
    for (const auto &m : joints()) {
        raw->inject(m);
    }
    drain(*raw, dispatcher);
    REQUIRE_FALSE(manager.get_snapshot());
    raw->on_request(id, [](const Message &req) {
        if (req.data[0] != 0x45) {
            return std::vector<Message>{};
        }
        auto out = joints();
        out[0].is_extended = true;
        out[1].is_fd = true;
        out[2].is_brs = true;
        out[3].dlc = 6;
        out[4].dlc = 9;
        return out;
    });
    REQUIRE_THROWS(manager.get_blocking(40ms));
    REQUIRE_FALSE(manager.get_snapshot());
    const auto count = raw->sent().size();
    for (const auto &m : joints()) {
        raw->inject(m);
    }
    drain(*raw, dispatcher);
    REQUIRE_THROWS(manager.get_blocking(100ms));
    REQUIRE(raw->sent().size() == count);
    REQUIRE_FALSE(manager.get_snapshot());
}

TEST_CASE("comm: l20 joint duplicate conflict quarantines actual request", "[comm][l20][rfix]") {
    for (bool conflict : {false, true}) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        raw->on_request(id, [=](const Message &req) {
            if (req.data[0] != 0x45) {
                return std::vector<Message>{};
            }
            auto out = joints();
            auto duplicate = out.front();
            if (conflict) {
                ++duplicate.data[1];
            }
            out.insert(out.begin() + 1, duplicate);
            return out;
        });
        CANDispatcher dispatcher(std::move(transport));
        AngleManager manager(id, dispatcher, test_util::unique_mapping_path());
        manager.start();
        if (conflict) {
            REQUIRE_THROWS(manager.get_blocking(100ms));
            REQUIRE_FALSE(manager.get_snapshot());
            REQUIRE_THROWS(manager.get_blocking(100ms));
            REQUIRE(raw->sent().size() == 5);
        } else {
            REQUIRE_NOTHROW(manager.get_blocking(100ms));
        }
    }
}

TEST_CASE("comm: l20 tactile request owns out of order rows and identical duplicates", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    SingleForceSensorManager manager(id, dispatcher, 0xB1);
    manager.start();
    for (const auto &m : tactile()) {
        raw->inject(m);
    }
    drain(*raw, dispatcher);
    REQUIRE_FALSE(manager.get_snapshot());
    raw->on_request(id, [](const Message &) {
        auto out = tactile();
        std::reverse(out.begin(), out.end());
        out.insert(out.begin() + 2, out.front());
        return out;
    });
    const auto data = manager.get_blocking(100ms);
    REQUIRE(data.values[0][0] == 1);
    REQUIRE(data.values[11][5] == 12);
}

TEST_CASE("comm: l20 tactile rejects wrong formats and conflicting duplicates", "[comm][l20][rfix]") {
    for (int kind = 0; kind < 6; ++kind) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        raw->on_request(id, [=](const Message &) {
            auto out = tactile();
            if (kind == 0) {
                auto bad = out[3];
                ++bad.data[2];
                out.insert(out.begin() + 4, bad);
            }
            if (kind == 1) {
                out[3].is_extended = true;
            }
            if (kind == 2) {
                out[3].is_fd = true;
            }
            if (kind == 3) {
                out[3].is_brs = true;
            }
            if (kind == 4) {
                out[3].dlc = 9;
            }
            if (kind == 5) {
                out[3].data[1] |= 1;
            }
            return out;
        });
        CANDispatcher dispatcher(std::move(transport));
        SingleForceSensorManager manager(id, dispatcher, 0xB1);
        manager.start();
        REQUIRE_THROWS(manager.get_blocking(25ms));
        REQUIRE_FALSE(manager.get_snapshot());
        for (const auto &m : tactile()) {
            raw->inject(m);
        }
        drain(*raw, dispatcher);
        REQUIRE_THROWS(manager.get_blocking(100ms));
        REQUIRE(raw->sent().size() == 1);
        REQUIRE_FALSE(manager.get_snapshot());
    }
}

TEST_CASE("comm: l20 timeout includes blocked backend and cancels unsent fragments", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->arm_tx_gate();
    CANDispatcher dispatcher(std::move(transport));
    AngleManager manager(id, dispatcher, test_util::unique_mapping_path());
    manager.start();
    auto request = std::async(std::launch::async, [&] { return fails([&] { (void)manager.get_blocking(30ms); }); });
    const bool entered = raw->wait_until_tx_entered(1s);
    // Even a full immediate reply cannot complete a synchronous API while its
    // physical backend call is still blocked.
    for (const auto &m : joints()) {
        raw->inject(m);
    }
    const auto ready = request.wait_for(150ms);
    raw->release_tx_gate(); // unconditional cleanup even when the regression fails
    const bool failed = request.get();
    REQUIRE(entered);
    REQUIRE(ready == std::future_status::ready);
    REQUIRE(failed);
    REQUIRE_THROWS(manager.get_blocking(100ms));
    // Drain the shared writer before examining its physical frame count.
    auto session = linkerbot::transport::Session::borrow(dispatcher);
    Message marker;
    marker.id = 0x700;
    session->send({{marker}});
    const auto sent = raw->sent();
    REQUIRE(std::count_if(sent.begin(), sent.end(), [](const Message &m) { return m.id == id; }) <= 1);
}

TEST_CASE("comm: l20 queued request deadline and stop wake active and queued callers", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::promise<void> sent;
    raw->on_request(id, [&](const Message &m) {
        if (m.data[0] == 0x45) {
            sent.set_value();
        }
        return std::vector<Message>{};
    });
    CANDispatcher dispatcher(std::move(transport));
    AngleManager manager(id, dispatcher, test_util::unique_mapping_path());
    manager.start();
    auto first = std::async(std::launch::async, [&] { return fails([&] { (void)manager.get_blocking(5s); }); });
    const auto started = sent.get_future().wait_for(1s);
    auto short_call = std::async(std::launch::async, [&] { return fails([&] { (void)manager.get_blocking(25ms); }); });
    const auto short_ready = short_call.wait_for(150ms);
    auto queued = std::async(std::launch::async, [&] { return fails([&] { (void)manager.get_blocking(5s); }); });
    manager.stop();
    const auto first_ready = first.wait_for(150ms), queued_ready = queued.wait_for(150ms);
    REQUIRE(started == std::future_status::ready);
    REQUIRE(short_ready == std::future_status::ready);
    REQUIRE(first_ready == std::future_status::ready);
    REQUIRE(queued_ready == std::future_status::ready);
    REQUIRE(first.get());
    REQUIRE(short_call.get());
    REQUIRE(queued.get());
    REQUIRE(raw->sent().size() == 5);
}

TEST_CASE("comm: l20 version shares one budget across all four fields", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(id, [](const Message &m) {
        std::this_thread::sleep_for(25ms);
        if (m.data[0] == 0xC0) {
            std::vector<Message> out;
            for (int i = 0; i < 4; ++i) {
                Message r;
                r.id = id;
                r.dlc = 8;
                r.data[0] = 0xC0;
                r.data[1] = i;
                out.push_back(r);
            }
            return out;
        }
        Message r;
        r.id = id;
        r.dlc = 4;
        r.data[0] = m.data[0];
        r.data[1] = 1;
        return std::vector<Message>{r};
    });
    CANDispatcher dispatcher(std::move(transport));
    VersionManager manager(id, dispatcher);
    manager.start();
    const auto begin = std::chrono::steady_clock::now();
    REQUIRE_THROWS(manager.get_device_info(60ms));
    REQUIRE(std::chrono::steady_clock::now() - begin < 150ms);
    REQUIRE(raw->sent().size() <= 3);
}

TEST_CASE("comm: l20 serial fragments require exact format and reject conflicts", "[comm][l20][rfix]") {
    for (int kind = 0; kind < 4; ++kind) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        raw->on_request(id, [=](const Message &m) {
            if (m.data[0] != 0xC0) {
                Message r;
                r.id = id;
                r.dlc = 4;
                r.data[0] = m.data[0];
                return std::vector<Message>{r};
            }
            std::vector<Message> out;
            for (int i = 0; i < 4; ++i) {
                Message r;
                r.id = id;
                r.dlc = 8;
                r.data[0] = 0xC0;
                r.data[1] = i;
                out.push_back(r);
            }
            if (kind == 0) {
                out[1].dlc = 7;
            }
            if (kind == 1) {
                out[1].is_fd = true;
            }
            if (kind == 2) {
                auto conflict = out[1];
                conflict.data[2] = 1;
                out.insert(out.begin() + 2, conflict);
            }
            if (kind == 3) {
                auto same = out[1];
                out.insert(out.begin() + 2, same);
            }
            return out;
        });
        CANDispatcher dispatcher(std::move(transport));
        VersionManager manager(id, dispatcher);
        manager.start();
        if (kind == 3) {
            REQUIRE_NOTHROW(manager.get_device_info(100ms));
        } else {
            REQUIRE_THROWS(manager.get_device_info(35ms));
        }
    }
}

TEST_CASE("comm: l20 copied RX callback is cancelled before manager destruction", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    std::promise<void> entered, release;
    auto proceed = release.get_future().share();
    const auto sub = dispatcher.subscribe([&](const Message &) {
        entered.set_value();
        proceed.wait();
    });
    auto manager = std::make_unique<AngleManager>(id, dispatcher, test_util::unique_mapping_path());
    manager->start();
    raw->inject(joints().front());
    const auto blocked = entered.get_future().wait_for(1s);
    auto destroyed = std::async(std::launch::async, [&] { manager.reset(); });
    const auto ready = destroyed.wait_for(150ms);
    release.set_value();
    destroyed.get();
    dispatcher.unsubscribe(sub);
    REQUIRE(blocked == std::future_status::ready);
    REQUIRE(ready == std::future_status::ready);
}

TEST_CASE("comm: l20 manager stop from RX callback does not self join", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    AngleManager manager(id, dispatcher, test_util::unique_mapping_path());
    manager.start();
    std::promise<void> done;
    const auto sub = dispatcher.subscribe([&](const Message &) {
        manager.stop();
        done.set_value();
    });
    raw->inject(joints().front());
    REQUIRE(done.get_future().wait_for(1s) == std::future_status::ready);
    dispatcher.unsubscribe(sub);
}

TEST_CASE("comm: l20 public five frame helpers retain contiguous paced batches", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::promise<void> first;
    raw->on_request(id, [&](const Message &m) {
        if (m.data[0] == 0x41) {
            first.set_value();
        }
        return std::vector<Message>{};
    });
    CANDispatcher dispatcher(std::move(transport));
    std::array<uint8_t, kJointCount> values{};
    auto a = std::async(std::launch::async, [&] { send_joint_frames(dispatcher, id, kAngleFrames, values, 10ms); });
    REQUIRE(first.get_future().wait_for(1s) == std::future_status::ready);
    auto b = std::async(std::launch::async, [&] { send_joint_frames(dispatcher, id + 1, kAngleFrames, values, 10ms); });
    a.get();
    b.get();
    const auto sent = raw->sent();
    REQUIRE(sent.size() == 10);
    for (size_t i = 0; i < 10; ++i) {
        REQUIRE(sent[i].id == (i < 5 ? id : id + 1));
    }
}

TEST_CASE("comm: l20 stop interrupts polling sleep and rejects invalid or stopped starts", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::promise<void> sent;
    raw->on_request(id, [&](const Message &m) {
        if (m.data[0] == 0x45) {
            sent.set_value();
        }
        return l20_comm::five_handler(id, 0x41, 20)(m);
    });
    L20 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    REQUIRE_THROWS_AS(hand.start_polling({SensorSource::kAngle}, 0ms), linkerbot::hand::ValidationError);
    hand.start_polling({SensorSource::kAngle, SensorSource::kAngle}, 3s);
    REQUIRE(sent.get_future().wait_for(1s) == std::future_status::ready);
    const auto start = std::chrono::steady_clock::now();
    hand.stop();
    REQUIRE(std::chrono::steady_clock::now() - start < 250ms);
    REQUIRE(raw->sent().size() == 5);
    REQUIRE_THROWS(hand.start_polling({SensorSource::kAngle}, 10ms));
}

TEST_CASE("comm: l20 polling faults and close failure remain observable without terminate", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->fail_writes("offline polling failure");
    L20 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    hand.start_polling({SensorSource::kAngle}, 10ms);
    REQUIRE(hand.wait_for_polling_fault(1s));
    REQUIRE(hand.last_polling_error());
    raw->fail_next_closes(1);
    REQUIRE_THROWS(hand.stop());
    REQUIRE_NOTHROW(hand.stop());
    auto fail_close = std::make_unique<FakeTransport>();
    fail_close->fail_next_closes(1);
    { L20 another(Side::kLeft, std::move(fail_close), "fake", test_util::unique_mapping_path()); }
}

TEST_CASE("comm: l20 force composite excludes individual finger requests", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::promise<void> thumb_sent;
    raw->on_request(id, [&](const Message &m) {
        if (m.data[0] == 0xB1) {
            thumb_sent.set_value();
            return std::vector<Message>{};
        }
        return tactile(m.data[0]);
    });
    CANDispatcher dispatcher(std::move(transport));
    ForceSensorManager manager(id, dispatcher);
    manager.start();
    auto all = std::async(std::launch::async, [&] { return manager.get_blocking(1s); });
    const auto started = thumb_sent.get_future().wait_for(1s);
    auto individual = std::async(std::launch::async,
                                 [&] { return fails([&] { (void)manager.get_finger("index").get_blocking(25ms); }); });
    const auto ready = individual.wait_for(150ms);
    for (const auto &m : tactile()) {
        raw->inject(m);
    }
    const auto result = all.get();
    REQUIRE(started == std::future_status::ready);
    REQUIRE(ready == std::future_status::ready);
    REQUIRE(individual.get());
    REQUIRE(result.pinky.values[11][5] == 12);
    const auto sent = raw->sent();
    REQUIRE(sent.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(sent[i].data[0] == 0xB1 + i);
    }
}

TEST_CASE("comm: l20 force composite consumes one total backend budget", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(id, [](const Message &m) {
        std::this_thread::sleep_for(25ms);
        return tactile(m.data[0]);
    });
    CANDispatcher dispatcher(std::move(transport));
    ForceSensorManager manager(id, dispatcher);
    manager.start();
    const auto start = std::chrono::steady_clock::now();
    REQUIRE_THROWS(manager.get_blocking(60ms));
    REQUIRE(std::chrono::steady_clock::now() - start < 150ms);
    REQUIRE(raw->sent().size() <= 3);
}

TEST_CASE("comm: l20 stop wakes all domain tactile and version waiters", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::mutex mutex;
    std::condition_variable cv;
    std::set<uint8_t> seen;
    raw->on_request(id, [&](const Message &m) {
        const std::lock_guard lock(mutex);
        seen.insert(m.data[0]);
        cv.notify_all();
        return std::vector<Message>{};
    });
    L20 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    std::vector<std::future<bool>> calls;
    const auto launch = [&](auto &manager) {
        calls.push_back(
            std::async(std::launch::async, [&manager] { return fails([&] { (void)manager.get_blocking(5s); }); }));
    };
    launch(hand.angle);
    launch(hand.speed);
    launch(hand.current);
    launch(hand.temperature);
    launch(hand.fault);
    launch(hand.force_sensor);
    calls.push_back(
        std::async(std::launch::async, [&] { return fails([&] { (void)hand.version.get_device_info(5s); }); }));
    bool started;
    {
        std::unique_lock lock(mutex);
        started = cv.wait_for(lock, 1s, [&] {
            return seen.count(0x41) && seen.count(0x49) && seen.count(0x51) && seen.count(0x59) && seen.count(0x61) &&
                   seen.count(0xB1) && seen.count(0xC2);
        });
    }
    hand.stop();
    REQUIRE(started);
    for (auto &call : calls) {
        REQUIRE(call.wait_for(150ms) == std::future_status::ready);
        REQUIRE(call.get());
    }
}

TEST_CASE("comm: l20 stop polling cancels a claimed backend without waiting for it", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->arm_tx_gate();
    L20 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    hand.start_polling({SensorSource::kAngle}, 3s);
    const bool entered = raw->wait_until_tx_entered(1s);
    auto stopped = std::async(std::launch::async, [&] { hand.stop_polling(); });
    const auto ready = stopped.wait_for(100ms);
    raw->release_tx_gate();
    stopped.get();
    REQUIRE(entered);
    REQUIRE(ready == std::future_status::ready);
    REQUIRE_FALSE(hand.polling_faulted());
}

TEST_CASE("comm: l20 consecutive send-only targets do not require firmware echoes", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    AngleManager manager(id, dispatcher, test_util::unique_mapping_path());
    manager.start();
    std::array<uint8_t, kJointCount> values{};
    manager.set_angles(values);
    values.fill(100);
    manager.set_angles(values);
    REQUIRE(raw->sent().size() == 10);
    REQUIRE_THROWS(manager.get_blocking(50ms)); // cannot identify the earlier write's late echo
    REQUIRE(raw->sent().size() == 10);
}

namespace {
    // Explicit member-pointer instantiation exposes only the pre-existing private
    // sink for this regression, without adding a test-only production API.
    template <class Tag, typename Tag::Type member> struct PrivateMember {
        friend typename Tag::Type access(Tag) { return member; }
    };
    struct AngleSink {
        using Type = void (AngleManager::*)(std::function<void(const AngleData &)>);
        friend Type access(AngleSink);
    };
    struct TactileSink {
        using Type = void (SingleForceSensorManager::*)(std::function<void(const ForceSensorData &)>);
        friend Type access(TactileSink);
    };
    template struct PrivateMember<AngleSink, &AngleManager::set_event_sink>;
    template struct PrivateMember<TactileSink, &SingleForceSensorManager::set_event_sink>;

    template <class Manager, class Tag> void sink_stop_race(Manager &manager, Tag tag) {
        std::promise<void> entered, release, sink_done;
        auto proceed = release.get_future().share();
        std::atomic<bool> rejected{false};
        (manager.*access(tag))([&](const auto &) {
            entered.set_value();
            proceed.wait();
            rejected.store(fails([&] { (void)manager.get_blocking(1s); }));
            manager.stop(); // cancellation from this very manager's sink
            sink_done.set_value();
        });
        auto request = std::async(std::launch::async, [&] { return fails([&] { (void)manager.get_blocking(1s); }); });
        const auto active = entered.get_future().wait_for(1s);
        std::promise<void> stopping;
        auto stopped = std::async(std::launch::async, [&] {
            stopping.set_value();
            manager.stop();
        });
        stopping.get_future().wait();
        const auto blocked = stopped.wait_for(20ms);
        release.set_value();
        const auto completed = sink_done.get_future().wait_for(1s);
        stopped.get();
        (void)request.get();
        REQUIRE(active == std::future_status::ready);
        REQUIRE(blocked == std::future_status::timeout); // cancellation barrier waits for entered sink
        REQUIRE(completed == std::future_status::ready);
        REQUIRE(rejected.load());
    }
}

TEST_CASE("comm: l20 angle sink can stop while external stop waits for callback", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(id, l20_comm::five_handler(id, 0x41, 30));
    CANDispatcher dispatcher(std::move(transport));
    AngleManager manager(id, dispatcher, test_util::unique_mapping_path());
    manager.start();
    sink_stop_race(manager, AngleSink{});
}

TEST_CASE("comm: l20 tactile sink can stop while external stop waits for callback", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(id, [](const Message &) { return tactile(); });
    CANDispatcher dispatcher(std::move(transport));
    SingleForceSensorManager manager(id, dispatcher, 0xB1);
    manager.start();
    sink_stop_race(manager, TactileSink{});
}

TEST_CASE("comm: l20 stopped stream references remain alive after replacement", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    L20 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    auto &old = hand.stream();
    hand.stop_stream();
    auto &next = hand.stream();
    REQUIRE(&old != &next);
    REQUIRE(old.closed());
    REQUIRE_FALSE(next.closed());
    hand.stop();
    REQUIRE(old.closed());
    REQUIRE(next.closed());
    REQUIRE_THROWS(hand.stream());
}

TEST_CASE("comm: l20 polling reports receive closed and nonstandard transport failures", "[comm][l20][rfix]") {
    SECTION("receive failure") {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        transport->on_request(id, l20_comm::five_handler(id, 0x41, 30));
        L20 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
        raw->fail_reads("offline receive failure");
        hand.start_polling({SensorSource::kAngle}, 5ms);
        REQUIRE(hand.wait_for_polling_fault(1s));
        REQUIRE(hand.last_polling_error());
    }
    SECTION("closed transport") {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        L20 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
        raw->close();
        hand.start_polling({SensorSource::kAngle}, 5ms);
        REQUIRE(hand.wait_for_polling_fault(1s));
        REQUIRE(hand.last_polling_error());
    }
    SECTION("nonstandard write exception") {
        class NonstandardTransport : public FakeTransport {
            void write(const Message &) override { throw 7; }
        };
        L20 hand(Side::kLeft, std::make_unique<NonstandardTransport>(), "fake", test_util::unique_mapping_path());
        hand.start_polling({SensorSource::kAngle}, 5ms);
        REQUIRE(hand.wait_for_polling_fault(1s));
        REQUIRE(hand.last_polling_error());
    }
}

TEST_CASE("comm: l20 manager final Session release from RX is drained externally", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    auto manager = std::make_unique<AngleManager>(id, dispatcher, test_util::unique_mapping_path());
    manager->start();
    std::promise<void> destroyed;
    const auto sub = dispatcher.subscribe([&](const Message &) {
        manager.reset();
        destroyed.set_value();
    });
    raw->inject(joints().front());
    const auto completed = destroyed.get_future().wait_for(1s);
    dispatcher.unsubscribe(sub);
    // Parent-owned comm fix: borrowed Dispatcher must outlive deferred Session cleanup.
    linkerbot::transport::Session::drain_deferred_cleanup();
    REQUIRE(completed == std::future_status::ready);
    REQUIRE_FALSE(manager);
}

TEST_CASE("comm: l20 force snapshot preserves last complete batch after partial failure", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::atomic<int> generation{1};
    raw->on_request(id, [&](const Message &m) {
        auto out = tactile(m.data[0]);
        for (auto &frame : out) {
            std::fill_n(frame.data.begin() + 2, 6, static_cast<uint8_t>(generation.load()));
        }
        if (generation.load() == 3 && m.data[0] == 0xB3) {
            out.pop_back();
        }
        return out;
    });
    CANDispatcher dispatcher(std::move(transport));
    ForceSensorManager manager(id, dispatcher);
    manager.start();
    REQUIRE_FALSE(manager.get_snapshot());
    (void)manager.get_blocking(200ms);
    REQUIRE(manager.get_snapshot()->thumb.values[0][0] == 1);
    REQUIRE(manager.get_snapshot()->pinky.values[0][0] == 1);
    generation.store(2);
    (void)manager.get_finger("thumb").get_blocking(100ms);
    REQUIRE(manager.get_finger("thumb").get_snapshot()->values[0][0] == 2);
    REQUIRE(manager.get_snapshot()->thumb.values[0][0] == 1); // single update is not an all-finger sample
    generation.store(3);
    REQUIRE_THROWS(manager.get_blocking(50ms));
    REQUIRE(manager.get_finger("thumb").get_snapshot()->values[0][0] == 3);
    REQUIRE(manager.get_snapshot()->thumb.values[0][0] == 1);
    REQUIRE(manager.get_snapshot()->index.values[0][0] == 1);
    REQUIRE(manager.get_snapshot()->middle.values[0][0] == 1);
    REQUIRE(manager.get_snapshot()->pinky.values[0][0] == 1);
}

TEST_CASE("comm: l20 force polling keeps previous full snapshot when next cycle loses a row", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::atomic<int> generation{1};
    raw->on_request(id, [&](const Message &m) {
        auto out = tactile(m.data[0]);
        for (auto &frame : out) {
            std::fill_n(frame.data.begin() + 2, 6, static_cast<uint8_t>(generation.load()));
        }
        if (generation.load() == 2 && m.data[0] == 0xB3) {
            out.pop_back();
        }
        return out;
    });
    L20 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    (void)hand.force_sensor.get_blocking(200ms);
    generation.store(2);
    hand.start_polling({SensorSource::kForceSensor}, 10ms);
    const bool faulted = hand.wait_for_polling_fault(1s);
    hand.stop_polling();
    REQUIRE(faulted);
    REQUIRE(hand.force_sensor.get_finger("thumb").get_snapshot()->values[0][0] == 2);
    const auto whole = hand.force_sensor.get_snapshot();
    REQUIRE(whole);
    REQUIRE(whole->thumb.values[0][0] == 1);
    REQUIRE(whole->index.values[0][0] == 1);
    REQUIRE(whole->middle.values[0][0] == 1);
    REQUIRE(whole->pinky.values[0][0] == 1);
}

TEST_CASE("comm: l20 destruction cancels and drains a blocking manager request", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::promise<void> sent;
    raw->on_request(id, [&](const Message &m) {
        if (m.data[0] == 0x45) {
            sent.set_value();
        }
        return std::vector<Message>{};
    });
    CANDispatcher dispatcher(std::move(transport));
    auto manager = std::make_unique<AngleManager>(id, dispatcher, test_util::unique_mapping_path());
    manager->start();
    auto *target = manager.get();
    auto request = std::async(std::launch::async, [&] { return fails([&] { (void)target->get_blocking(5s); }); });
    const auto active = sent.get_future().wait_for(1s);
    manager.reset();
    REQUIRE(active == std::future_status::ready);
    REQUIRE(request.wait_for(100ms) == std::future_status::ready);
    REQUIRE(request.get());
}

namespace {
    struct ForcePoll {
        using Type = void (ForceSensorManager::*)();
        friend Type access(ForcePoll);
    };
    struct ForceSink {
        using Type = void (ForceSensorManager::*)(std::function<void(const AllFingersData &)>);
        friend Type access(ForceSink);
    };
    template struct PrivateMember<ForcePoll, &ForceSensorManager::send_sense_request>;
    template struct PrivateMember<ForceSink, &ForceSensorManager::set_event_sink>;
}

TEST_CASE("comm: l20 pending polling batch keeps its original thumb", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    ForceSensorManager manager(id, dispatcher);
    manager.start();
    std::promise<void> complete;
    (manager.*access(ForceSink{}))([&](const auto &) { complete.set_value(); });
    (manager.*access(ForcePoll{}))();
    for (const auto &m : tactile(0xB1)) {
        raw->inject(m);
    }
    drain(*raw, dispatcher);
    raw->on_request(id, [&](const Message &m) {
        auto out = tactile(m.data[0]);
        for (auto &row : out) {
            std::fill_n(row.data.begin() + 2, 6, 99);
        }
        return out;
    });
    // A polling batch now shares the same exclusion as a blocking batch.
    // Its partial thumb must not admit a single-finger request.
    const auto sent = raw->sent().size();
    REQUIRE_THROWS(manager.get_finger("thumb").get_blocking(20ms));
    REQUIRE(raw->sent().size() == sent);
    for (uint8_t cmd = 0xB2; cmd <= 0xB5; ++cmd) {
        for (const auto &m : tactile(cmd)) {
            raw->inject(m);
        }
    }
    drain(*raw, dispatcher);
    REQUIRE(complete.get_future().wait_for(1s) == std::future_status::ready);
    const auto all = manager.get_snapshot();
    REQUIRE(all);
    REQUIRE(all->thumb.values[0][0] == 1);
    REQUIRE(all->pinky.values[0][0] == 1);
    REQUIRE(manager.get_finger("thumb").get_blocking(100ms).values[0][0] == 99);
}

namespace linkerbot::hand::l20 {
    struct ManagerTestAccess {
        static auto &gate(SingleForceSensorManager &m) { return m.channel_; }
        static void cancel(SingleForceSensorManager &m) { m.cancel_pending(); }
    };
}
TEST_CASE("comm: l20 single finger stop drains calls queued before runtime admission", "[comm][l20][rfix]") {
    struct Thumb : SingleForceSensorManager {
        Thumb(uint32_t id, CANDispatcher &dispatcher) : SingleForceSensorManager(id, dispatcher, 0xB1) {}
    };
    classic_regression::queued_destruction<Thumb, ManagerTestAccess>([](auto &m) { (void)m.get_blocking(5s); }, 3, 1);
}

TEST_CASE("comm: l20 hand cancels other domains before waiting for angle sink", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    std::promise<void> current_sent, sink_entered;
    auto current_was_sent = current_sent.get_future();
    transport->on_request(id, [&](const Message &m) {
        if (m.data[0] == 0x55) {
            current_sent.set_value();
        }
        if (m.data[0] == 0x45) {
            return joints();
        }
        return std::vector<Message>{};
    });
    L20 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    auto current =
        std::async(std::launch::async, [&] { return fails([&] { (void)hand.current.get_blocking(5s); }); }).share();
    const auto sent = current_was_sent.wait_for(1s);
    (hand.angle.*access(AngleSink{}))([&](const AngleData &) {
        sink_entered.set_value();
        current.wait();
    });
    auto angle = std::async(std::launch::async, [&] { return fails([&] { (void)hand.angle.get_blocking(5s); }); });
    const auto entered = sink_entered.get_future().wait_for(1s);
    auto stopped = std::async(std::launch::async, [&] { hand.stop(); });
    const auto ready = stopped.wait_for(300ms);
    stopped.get();
    (void)angle.get();
    REQUIRE(sent == std::future_status::ready);
    REQUIRE(entered == std::future_status::ready);
    REQUIRE(ready == std::future_status::ready);
    REQUIRE(current.get());
}

TEST_CASE("comm: l20 force stop cancels fingers before waiting for aggregate sink", "[comm][l20][rfix]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    ForceSensorManager manager(id, dispatcher);
    manager.start();
    std::promise<void> sink_entered, finger_sent;
    std::shared_future<bool> finger;
    (manager.*access(ForceSink{}))([&](const AllFingersData &) {
        sink_entered.set_value();
        finger.wait(); // original implementation waits for the five-second deadline
    });
    (manager.*access(ForcePoll{}))();
    for (uint8_t cmd = 0xB1; cmd <= 0xB4; ++cmd) {
        for (const auto &m : tactile(cmd)) {
            raw->inject(m);
        }
    }
    drain(*raw, dispatcher);
    raw->on_request(id, [&](const Message &) {
        finger_sent.set_value();
        return std::vector<Message>{};
    });
    finger = std::async(std::launch::async, [&] {
                 return fails([&] { (void)manager.get_finger("thumb").get_blocking(5s); });
             }).share();
    // Single-finger admission waits until the aggregate has all five fingers.
    for (const auto &m : tactile(0xB5)) {
        raw->inject(m);
    }
    const auto sent = finger_sent.get_future().wait_for(1s);
    const auto entered = sink_entered.get_future().wait_for(1s);
    auto stopped = std::async(std::launch::async, [&] { manager.stop(); });
    const auto ready = stopped.wait_for(300ms);
    stopped.get();
    REQUIRE(sent == std::future_status::ready);
    REQUIRE(entered == std::future_status::ready);
    REQUIRE(ready == std::future_status::ready);
    REQUIRE(finger.get());
}

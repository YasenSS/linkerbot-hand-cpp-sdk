#include "hand/o6/legacy_regressions.hpp"
#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>
#include <future>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>
using namespace std::chrono_literals;
using namespace linkerbot::hand;
using linkerbot::can::FakeTransport;
using linkerbot::can::Message;

TEST_CASE("comm: L20lite shared Session close is device local", "[comm][l20lite][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    raw->on_request(0x27, [](const Message &m) {
        auto reply = m;
        reply.dlc = 6;
        return std::vector<Message>{reply};
    });
    auto session = linkerbot::transport::Session::own(std::move(fake));
    l20lite::L20lite left(l20lite::Side::kLeft, session, "fake", test_util::unique_mapping_path());
    l20lite::L20lite right(l20lite::Side::kRight, session, "fake", test_util::unique_mapping_path());
    std::promise<void> entered;
    raw->on_request(0x28, [&](const Message &m) {
        if (m.data[0] == 6) {
            entered.set_value();
        }
        return std::vector<Message>{};
    });
    auto waiting = std::async(std::launch::async, [&] {
        try {
            (void)left.speed.get_blocking(5s);
            return false;
        } catch (const linkerbot::hand::LifecycleError &) {
            return true;
        }
    });
    REQUIRE(entered.get_future().wait_for(1s) == std::future_status::ready);
    left.stop();
    REQUIRE(waiting.wait_for(200ms) == std::future_status::ready);
    REQUIRE(waiting.get());
    REQUIRE(left.runtime_health().in_flight == 0);
    REQUIRE_NOTHROW(right.speed.get_blocking(500ms));
    REQUIRE_THROWS(left.config.save());
    REQUIRE(right.runtime_health().state == runtime::DeviceState::ready);
}

TEST_CASE("comm: L20lite standalone manager uses the same runtime and cleans timeout",
          "[comm][l20lite][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    linkerbot::can::CANDispatcher dispatcher(std::move(fake));
    auto core = runtime::DeviceRuntime::borrow(dispatcher, "l20lite/40", {{0x28, false}});
    l20lite::SpeedManager speed(0x28, dispatcher);
    speed.start();
    REQUIRE_THROWS(speed.get_blocking(10ms));
    REQUIRE(core->runtime_health().timeouts == 1);
    REQUIRE(core->runtime_health().in_flight == 0);
    const auto sent = raw->sent().size();
    REQUIRE_THROWS(speed.get_blocking(10ms));
    REQUIRE(raw->sent().size() == sent);
    speed.stop();
}

namespace {
    class FinalWriteFailure : public FakeTransport {
    public:
        std::atomic<bool> fail{false};
        void write(const Message &m) override {
            FakeTransport::write(m);
            if (fail && m.data[0] == 0xB5) {
                throw std::runtime_error("final frame backend failure");
            }
        }
    };
}
TEST_CASE("comm: L20lite full tactile replies cannot hide a failed final send", "[comm][l20lite][runtime-migration]") {
    auto fake = std::make_unique<FinalWriteFailure>();
    auto *raw = fake.get();
    raw->on_request(0x28, [](const Message &m) { return classic_regression::rows(m, 12, 6); });
    l20lite::L20lite hand(l20lite::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    const auto first = hand.force_sensor.get_blocking(500ms);
    REQUIRE(first.thumb.timestamp != runtime::Deadline{});
    raw->fail = true;
    REQUIRE_THROWS(hand.force_sensor.get_blocking(500ms));
    REQUIRE(hand.runtime_health().in_flight == 0);
    auto snapshot = hand.force_sensor.get_snapshot();
    REQUIRE(snapshot.has_value());
    REQUIRE(test_support::require_value(snapshot).thumb.timestamp == first.thumb.timestamp);
}

TEST_CASE("comm: L20lite polling fault remains isolated to its source", "[comm][l20lite][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    std::promise<void> speed_seen;
    std::atomic<bool> once{false};
    raw->on_request(0x28, [&](const Message &m) {
        if (m.data[0] == 5 || m.data[0] == 6) {
            if (!once.exchange(true)) {
                speed_seen.set_value();
            }
            auto reply = m;
            reply.dlc = 6;
            return std::vector<Message>{reply};
        }
        return std::vector<Message>{};
    });
    l20lite::L20lite hand(l20lite::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    // A stopped manager causes only its polling task to fail.
    hand.current.stop();
    hand.start_polling({l20lite::SensorSource::kCurrent, l20lite::SensorSource::kSpeed}, 10ms);
    REQUIRE(hand.wait_for_polling_fault(500ms));
    REQUIRE(speed_seen.get_future().wait_for(500ms) == std::future_status::ready);
    REQUIRE(hand.runtime_health().state == runtime::DeviceState::ready);
    REQUIRE(hand.last_polling_error().has_value());
    hand.stop_polling();
    hand.start_polling({l20lite::SensorSource::kSpeed}, 100ms);
    REQUIRE_FALSE(hand.polling_faulted());
    hand.stop_polling();
}

TEST_CASE("comm: L20Lite tip force and config queues share deadlines and stop cancels active calls",
          "[comm][l20lite][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    l20lite::L20lite hand(l20lite::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    std::promise<void> sent;
    std::atomic<bool> once{false};
    raw->on_request(0x28, [&](const Message &m) {
        if ((m.data[0] == 0x23 || m.data[0] == 0xC3) && !once.exchange(true)) {
            sent.set_value();
        }
        return std::vector<Message>{};
    });
    std::function<void(std::chrono::milliseconds)> query;
    std::function<void()> stop;
    SECTION("tip force") {
        query = [&](auto t) { (void)hand.tip_force.get_blocking(t); };
        stop = [&] { hand.tip_force.stop(); };
    }
    SECTION("config") {
        query = [&](auto t) { (void)hand.config.set_id(0x28, t); };
        stop = [&] { hand.config.stop(); };
    }
    auto active = std::async(std::launch::async, [&] {
        try {
            query(5s);
            return false;
        } catch (const linkerbot::hand::LifecycleError &) {
            return true;
        }
    });
    REQUIRE(sent.get_future().wait_for(1s) == std::future_status::ready);
    const auto count = raw->sent().size();
    const auto begin = runtime::Clock::now();
    REQUIRE_THROWS_AS(query(20ms), linkerbot::internal::DataRelayTimeout);
    REQUIRE(runtime::Clock::now() - begin < 300ms);
    REQUIRE(raw->sent().size() == count);
    stop();
    REQUIRE(active.wait_for(300ms) == std::future_status::ready);
    REQUIRE(active.get());
    REQUIRE(hand.runtime_health().in_flight == 0);
}

TEST_CASE("comm: L20Lite partial dual send fails and quarantines without leaking active requests",
          "[comm][l20lite][runtime-migration]") {
    class FailSecond : public FakeTransport {
    public:
        void write(const Message &m) override {
            FakeTransport::write(m);
            if (m.data[0] == 6) {
                throw std::runtime_error("partial write");
            }
        }
    };
    auto fake = std::make_unique<FailSecond>();
    auto *raw = fake.get();
    raw->on_request(0x28, [](const Message &m) {
        auto reply = m;
        reply.dlc = 6;
        return std::vector<Message>{reply};
    });
    l20lite::L20lite hand(l20lite::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    REQUIRE_THROWS(hand.speed.get_blocking(500ms));
    REQUIRE(hand.runtime_health().in_flight == 0);
    REQUIRE_FALSE(hand.speed.get_snapshot());
    const auto sent = raw->sent().size();
    REQUIRE_THROWS(hand.speed.get_blocking(20ms));
    REQUIRE(raw->sent().size() == sent);
}

TEST_CASE("comm: L20Lite stall writes queue within budget and cancel while getters read cache",
          "[comm][l20lite][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    raw->on_request(0x28, [](const Message &m) { return std::vector<Message>{m}; });
    l20lite::L20lite hand(l20lite::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    std::array<float, l20lite::kJointCount> times{};
    times.fill(500);
    hand.stall.set_times(times);
    const auto cached = hand.stall.get_time_snapshot();
    REQUIRE(cached);
    std::promise<void> sent;
    raw->on_request(0x28, [&](const Message &m) {
        if (m.data[0] == 0xC6 && m.data[1] == 2) {
            sent.set_value();
        }
        return std::vector<Message>{};
    });
    auto active = std::async(std::launch::async, [&] {
        try {
            hand.stall.set_times(times, 5s);
            return false;
        } catch (const linkerbot::hand::LifecycleError &) {
            return true;
        }
    });
    REQUIRE(sent.get_future().wait_for(1s) == std::future_status::ready);
    const auto count = raw->sent().size();
    REQUIRE(hand.stall.get_times_blocking(20ms).timestamp == test_support::require_value(cached).timestamp);
    REQUIRE_THROWS_AS(hand.stall.set_times(times, 20ms), linkerbot::hand::TimeoutError);
    REQUIRE(raw->sent().size() == count);
    hand.stall.stop();
    REQUIRE(active.wait_for(300ms) == std::future_status::ready);
    REQUIRE(active.get());
    REQUIRE(hand.runtime_health().in_flight == 0);
}

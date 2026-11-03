#include "hand/o6/legacy_regressions.hpp"
#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>
#include <future>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20/l20.hpp>
using namespace std::chrono_literals;
using namespace linkerbot::hand;
using linkerbot::can::FakeTransport;
using linkerbot::can::Message;

TEST_CASE("comm: L20 shared Session close is device local", "[comm][l20][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    raw->on_request(0x27, [](const Message &m) {
        auto reply = m;
        reply.dlc = 7;
        return std::vector<Message>{reply};
    });
    auto session = linkerbot::transport::Session::own(std::move(fake));
    l20::L20 left(l20::Side::kLeft, session, "fake", test_util::unique_mapping_path());
    l20::L20 right(l20::Side::kRight, session, "fake", test_util::unique_mapping_path());
    std::promise<void> entered;
    raw->on_request(0x28, [&](const Message &m) {
        if (m.data[0] == 0x4D) {
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
    REQUIRE_THROWS(left.speed.set_speeds(std::array<float, l20::kJointCount>{}));
    REQUIRE(right.runtime_health().state == runtime::DeviceState::ready);
}

TEST_CASE("comm: L20 standalone manager uses the same runtime and cleans timeout", "[comm][l20][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    linkerbot::can::CANDispatcher dispatcher(std::move(fake));
    auto core = runtime::DeviceRuntime::borrow(dispatcher, "l20/40", {{0x28, false}});
    l20::SpeedManager speed(0x28, dispatcher);
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
TEST_CASE("comm: L20 full tactile replies cannot hide a failed final send", "[comm][l20][runtime-migration]") {
    auto fake = std::make_unique<FinalWriteFailure>();
    auto *raw = fake.get();
    raw->on_request(0x28, [](const Message &m) { return classic_regression::rows(m, 12, 6); });
    l20::L20 hand(l20::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    const auto first = hand.force_sensor.get_blocking(500ms);
    REQUIRE(first.thumb.timestamp != runtime::Deadline{});
    raw->fail = true;
    REQUIRE_THROWS(hand.force_sensor.get_blocking(500ms));
    REQUIRE(hand.runtime_health().in_flight == 0);
    auto snapshot = hand.force_sensor.get_snapshot();
    REQUIRE(snapshot.has_value());
    REQUIRE(test_support::require_value(snapshot).thumb.timestamp == first.thumb.timestamp);
}

TEST_CASE("comm: L20 polling fault remains isolated to its source", "[comm][l20][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    std::promise<void> speed_seen;
    std::atomic<bool> once{false};
    raw->on_request(0x28, [&](const Message &m) {
        if (m.data[0] >= 0x49 && m.data[0] <= 0x4D) {
            if (!once.exchange(true)) {
                speed_seen.set_value();
            }
            auto reply = m;
            reply.dlc = 7;
            return std::vector<Message>{reply};
        }
        return std::vector<Message>{};
    });
    l20::L20 hand(l20::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    // A stopped manager causes only its polling task to fail.
    hand.current.stop();
    hand.start_polling({l20::SensorSource::kCurrent, l20::SensorSource::kSpeed}, 10ms);
    REQUIRE(hand.wait_for_polling_fault(500ms));
    REQUIRE(speed_seen.get_future().wait_for(500ms) == std::future_status::ready);
    REQUIRE(hand.runtime_health().state == runtime::DeviceState::ready);
    REQUIRE(hand.last_polling_error().has_value());
    hand.stop_polling();
    hand.start_polling({l20::SensorSource::kSpeed}, 100ms);
    REQUIRE_FALSE(hand.polling_faulted());
    hand.stop_polling();
}

#include <linkerbot/hand/o6/o6.hpp>
TEST_CASE("comm: L20 reply wait and close leave another model on the shared Session usable",
          "[comm][l20][runtime-migration]") {
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    auto session = linkerbot::transport::Session::own(std::move(fake));
    l20::L20 left(l20::Side::kLeft, session, "fake", test_util::unique_mapping_path());
    o6::O6 right(o6::Side::kRight, session, "fake", test_util::unique_mapping_path());
    raw->echo_cmd(0x27, 5, 6, 42);
    std::promise<void> batch_sent;
    raw->on_request(0x28, [&](const Message &m) {
        if (m.data[0] == 0x45) {
            batch_sent.set_value();
        }
        return std::vector<Message>{};
    });
    auto waiting = std::async(std::launch::async, [&] {
        try {
            (void)left.angle.get_blocking(5s);
            return false;
        } catch (const linkerbot::hand::LifecycleError &) {
            return true;
        }
    });
    REQUIRE(batch_sent.get_future().wait_for(1s) == std::future_status::ready);
    REQUIRE_NOTHROW(right.speed.get_blocking(200ms));
    left.stop();
    REQUIRE(waiting.wait_for(300ms) == std::future_status::ready);
    REQUIRE(waiting.get());
    REQUIRE_NOTHROW(right.speed.get_blocking(200ms));
    REQUIRE(right.runtime_health().state == runtime::DeviceState::ready);
}

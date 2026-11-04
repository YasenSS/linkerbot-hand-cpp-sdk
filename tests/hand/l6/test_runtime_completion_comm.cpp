#include "hand/support/temp_path.hpp"
#include <catch2/catch_test_macros.hpp>
#include <future>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l6/l6.hpp>
#include <linkerbot/hand/l6/protocol_backend.hpp>
#include <stdexcept>

using namespace std::chrono_literals;
using namespace linkerbot;
using namespace linkerbot::hand;
namespace linkerbot::hand::l6 {
    struct L6RuntimeTestAccess {
        static void feedback(AngleManager &manager, std::function<void()> fn) {
            manager.set_command_feedback_listener(std::move(fn));
        }
        static void reply(ConfigManager &manager, std::function<void(bool)> fn) {
            manager.set_reply_change_listener(std::move(fn));
        }
        static auto staged(ForceSensorManager &manager, runtime::Deadline deadline,
                           const std::function<void(size_t, runtime::Deadline)> &fn) {
            return manager.get_staged(deadline, fn);
        }
        static void angle_sink(AngleManager &manager, std::function<void(const l6::AngleData &)> fn) {
            manager.set_event_sink(std::move(fn));
        }
    };
}
namespace {
    struct ReplyHeader {
        uint8_t opcode;
        uint8_t length = 7;
    };
    can::Message reply_frame(const ReplyHeader &header) {
        can::Message m{};
        m.id = 0x28;
        m.dlc = header.length;
        m.data[0] = header.opcode;
        for (size_t i = 1; i < header.length; ++i) {
            m.data[i] = 1;
        }
        return m;
    }
    struct Gate {
        std::promise<void> arrived, release;
        std::shared_future<void> opened = release.get_future().share();
        void wait() {
            arrived.set_value();
            opened.wait();
        }
        bool entered() { return arrived.get_future().wait_for(2s) == std::future_status::ready; }
    };
}

TEST_CASE("comm: L6 stop drains post-response angle and config processing", "[comm][l6][completion]") {
    auto fake = std::make_unique<can::FakeTransport>();
    fake->on_request(0x28, [](const auto &m) {
        return m.data[0] == 1 ? std::vector<can::Message>{reply_frame({1})} : std::vector<can::Message>{};
    });
    l6::L6 hand(l6::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    Gate gate;
    bool config = false;
    SECTION("angle listener runs after the transaction completes") {}
    SECTION("reply configuration listener runs after the send completes") { config = true; }
    if (config) {
        l6::L6RuntimeTestAccess::reply(hand.config, [&](bool) { gate.wait(); });
    } else {
        l6::L6RuntimeTestAccess::feedback(hand.angle, [&] { gate.wait(); });
    }
    auto calling = std::async(std::launch::async, [&] {
        if (config) {
            hand.config.set_reply_enabled(true);
        } else {
            (void)hand.angle.set_angles_blocking({}, 1s);
        }
    });
    const bool entered = gate.entered();
    auto stopping = std::async(std::launch::async, [&] {
        if (config) {
            hand.config.stop();
        } else {
            hand.angle.stop();
        }
    });
    const auto blocked = stopping.wait_for(40ms);
    gate.release.set_value();
    calling.get();
    stopping.get();
    REQUIRE(entered);
    REQUIRE(blocked == std::future_status::timeout);
    REQUIRE(hand.runtime()->runtime_health().in_flight == 0);
}

TEST_CASE("comm: L6 force stop drains a composite between fingers", "[comm][l6][completion]") {
    auto fake = std::make_unique<can::FakeTransport>();
    auto *raw = fake.get();
    l6::L6 hand(l6::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    Gate gate;
    auto calling = std::async(std::launch::async, [&] {
        try {
            (void)l6::L6RuntimeTestAccess::staged(hand.force_sensor, runtime::Clock::now() + 2s,
                                                  [&](size_t, auto) { gate.wait(); });
            return false;
        } catch (const linkerbot::hand::LifecycleError &) {
            return true;
        }
    });
    const bool entered = gate.entered();
    auto stopping = std::async(std::launch::async, [&] { hand.force_sensor.stop(); });
    const auto blocked = stopping.wait_for(40ms);
    gate.release.set_value();
    const bool cancelled = calling.get();
    stopping.get();
    REQUIRE(entered);
    REQUIRE(blocked == std::future_status::timeout);
    REQUIRE(cancelled);
    REQUIRE(raw->sent().empty());
    REQUIRE(hand.runtime()->runtime_health().in_flight == 0);
}

TEST_CASE("comm: L6 close cancels all domains before waiting for an angle sink", "[comm][l6][completion]") {
    auto fake = std::make_unique<can::FakeTransport>();
    auto *raw = fake.get();
    std::promise<void> temperature_sent;
    raw->on_request(0x28, [&](const auto &m) {
        if (m.data[0] == l6::kTemperatureOpcode) {
            temperature_sent.set_value();
        }
        return m.data[0] == 1 ? std::vector<can::Message>{reply_frame({1})} : std::vector<can::Message>{};
    });
    l6::L6 hand(l6::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    auto reading = std::async(std::launch::async, [&] {
                       try {
                           (void)hand.temperature.get_blocking(5s);
                           return false;
                       } catch (const linkerbot::hand::LifecycleError &) {
                           return true;
                       } catch (...) {
                           return false;
                       }
                   }).share();
    const bool sent = temperature_sent.get_future().wait_for(1s) == std::future_status::ready;
    std::promise<void> sink_entered;
    std::atomic<bool> released_by_cancel{false};
    l6::L6RuntimeTestAccess::angle_sink(hand.angle, [&](const auto &) {
        sink_entered.set_value();
        released_by_cancel = reading.wait_for(500ms) == std::future_status::ready && reading.get();
    });
    (void)hand.angle.get_blocking(1s);
    const bool entered = sink_entered.get_future().wait_for(1s) == std::future_status::ready;
    hand.stop();
    REQUIRE(sent);
    REQUIRE(entered);
    REQUIRE(released_by_cancel);
    REQUIRE(reading.get());
    REQUIRE(hand.runtime()->runtime_health().in_flight == 0);
}

TEST_CASE("comm: L6 version stop cancels child query and queued composite together", "[comm][l6][completion]") {
    auto fake = std::make_unique<can::FakeTransport>();
    auto *raw = fake.get();
    std::promise<void> sent;
    raw->on_request(0x28, [&](const auto &) {
        sent.set_value();
        return std::vector<can::Message>{};
    });
    l6::L6 hand(l6::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    auto query = [&] {
        try {
            (void)hand.version.get_device_info(3s);
            return false;
        } catch (const linkerbot::hand::LifecycleError &) {
            return true;
        }
    };
    auto active = std::async(std::launch::async, query);
    const bool entered = sent.get_future().wait_for(1s) == std::future_status::ready;
    auto queued = std::async(std::launch::async, query);
    auto stop = std::async(std::launch::async, [&] { hand.version.stop(); });
    const bool prompt = stop.wait_for(200ms) == std::future_status::ready;
    // Bounded cleanup also makes the pre-fix failure diagnostic rather than a hang.
    if (!prompt) {
        hand.runtime()->cancel_all();
    }
    stop.get();
    REQUIRE(entered);
    REQUIRE(prompt);
    REQUIRE(active.get());
    REQUIRE(queued.get());
    REQUIRE(raw->sent().size() == 1);
    REQUIRE(hand.runtime()->runtime_health().in_flight == 0);
}

TEST_CASE("comm: L6 speed is cancelled before closing another manager callback", "[comm][l6][completion]") {
    auto fake = std::make_unique<can::FakeTransport>();
    fake->on_request(0x28, [](const auto &m) {
        return m.data[0] == 1 ? std::vector<can::Message>{reply_frame({1})} : std::vector<can::Message>{};
    });
    l6::L6 hand(l6::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    Gate gate;
    l6::L6RuntimeTestAccess::angle_sink(hand.angle, [&](const auto &) { gate.wait(); });
    (void)hand.angle.get_blocking(1s);
    const bool entered = gate.entered();
    auto stop = std::async(std::launch::async, [&] { hand.stop(); });
    // The temperature token is cancelled in the first phase before angle.stop's barrier.
    std::exception_ptr unexpected;
    const auto deadline = runtime::Clock::now() + 1s;
    while (hand.runtime()->runtime_health().state == runtime::DeviceState::ready && runtime::Clock::now() < deadline) {
        try {
            (void)hand.temperature.get_blocking(1ms);
        } catch (const linkerbot::hand::LifecycleError &) {
            break;
        } catch (const linkerbot::hand::TimeoutError &) {
            continue;
        } catch (...) {
            unexpected = std::current_exception();
            break;
        }
    }
    bool rejected = false;
    try {
        hand.speed.set_speeds(std::array<float, 6>{});
    } catch (const std::exception &) {
        rejected = true;
    }
    gate.release.set_value();
    stop.get();
    if (unexpected) {
        std::rethrow_exception(unexpected);
    }
    REQUIRE(entered);
    REQUIRE(rejected);
}

TEST_CASE("comm: L6 closed hand leaves another L6 on a shared Session usable", "[comm][l6][completion]") {
    auto fake = std::make_unique<can::FakeTransport>();
    auto *raw = fake.get();
    raw->on_request(0x27, [](const auto &request) {
        auto answer = reply_frame({request.data[0]});
        answer.id = 0x27;
        return std::vector<can::Message>{answer};
    });
    auto session = transport::Session::own(std::move(fake));
    l6::L6 left(l6::Side::kLeft, session, "fake", test_util::unique_mapping_path());
    l6::L6 right(l6::Side::kRight, session, "fake", test_util::unique_mapping_path());
    std::promise<void> sent;
    raw->on_request(0x28, [&](const auto &) {
        sent.set_value();
        return std::vector<can::Message>{};
    });
    auto query = std::async(std::launch::async, [&] {
        try {
            (void)left.current.get_blocking(2s);
            return false;
        } catch (const linkerbot::hand::LifecycleError &) {
            return true;
        }
    });
    const bool entered = sent.get_future().wait_for(1s) == std::future_status::ready;
    left.stop();
    REQUIRE(entered);
    REQUIRE(query.get());
    REQUIRE(left.runtime()->runtime_health().in_flight == 0);
    REQUIRE_THROWS(left.speed.set_speeds(std::array<float, 6>{}));
    REQUIRE_NOTHROW(right.current.get_blocking(1s));
    REQUIRE(right.runtime()->runtime_health().state == runtime::DeviceState::ready);
}

TEST_CASE("comm: L6 scalar timeouts and failed sends release their actual runtime slots", "[comm][l6][completion]") {
    for (int domain = 0; domain < 8; ++domain) {
        DYNAMIC_SECTION("domain " << domain) {
            auto fake = std::make_unique<can::FakeTransport>();
            bool fail = false;
            SECTION("timeout") {}
            SECTION("send failure") {
                fail = true;
                fake->fail_writes("L6 completion injected send failure");
            }
            l6::L6 hand(l6::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
            auto query = [&] {
                switch (domain) {
                case 0:
                    (void)hand.current.get_blocking(15ms);
                    break;
                case 1:
                    (void)hand.temperature.get_blocking(15ms);
                    break;
                case 2:
                    (void)hand.torque.get_blocking(15ms);
                    break;
                case 3:
                    (void)hand.fault.get_blocking(15ms);
                    break;
                case 4:
                    (void)hand.stall.get_times_blocking(15ms);
                    break;
                case 5:
                    (void)hand.stall.get_thresholds_blocking(15ms);
                    break;
                case 6:
                    (void)hand.stall.get_torques_blocking(15ms);
                    break;
                case 7:
                    (void)hand.config.set_receive_id(0x29, 15ms);
                    break;
                default:
                    throw std::logic_error("invalid test vector selector");
                }
            };
            if (fail) {
                REQUIRE_THROWS(query());
            } else {
                REQUIRE_THROWS_AS(query(), internal::DataRelayTimeout);
            }
            REQUIRE(hand.runtime()->runtime_health().in_flight == 0);
        }
    }
}

TEST_CASE("comm: L6 serial malformed response cannot become a successful identity", "[comm][l6][completion]") {
    for (bool mixed : {false, true}) {
        auto fake = std::make_unique<can::FakeTransport>();
        fake->on_request(0x28, [=](const auto &request) {
            const auto op = request.data[0];
            if (op != l6::kSerialOpcode) {
                auto m = reply_frame({op, static_cast<uint8_t>(op == l6::kPcbVersionOpcode ? 5 : 4)});
                return std::vector<can::Message>{m};
            }
            std::vector<can::Message> result;
            for (uint8_t marker : std::vector<uint8_t>{0, 1, uint8_t(mixed ? 12 : 2), 3}) {
                auto m = reply_frame({op, static_cast<uint8_t>((!mixed && marker == 0) ? 3 : 8)});
                m.data[1] = marker;
                result.push_back(m);
            }
            return result;
        });
        l6::L6 hand(l6::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
        REQUIRE_THROWS(hand.version.get_device_info(100ms));
        REQUIRE(hand.runtime()->runtime_health().in_flight == 0);
        auto cached = hand.runtime()->snapshot("version.serial");
        REQUIRE((!cached || !cached->valid));
    }
}

TEST_CASE("comm: L6 event-thread destruction drains other facade callers", "[comm][l6][completion]") {
    auto fake = std::make_unique<can::FakeTransport>();
    fake->on_request(0x28, [](const auto &m) { return std::vector<can::Message>{reply_frame({m.data[0]})}; });
    auto hand = std::make_unique<l6::L6>(l6::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    auto core = hand->runtime();
    (void)hand->current.get_blocking(1s);
    const auto provenance = core->response_provenance();
    Gate gate;
    l6::L6RuntimeTestAccess::reply(hand->config, [&](bool) { gate.wait(); });
    auto *pointer = hand.get();
    auto calling = std::async(std::launch::async, [&] { pointer->config.set_reply_enabled(true); });
    const bool entered = gate.entered();
    std::promise<void> destroying, destroyed;
    auto began = destroying.get_future();
    auto done = destroyed.get_future();
    const auto subscription = core->subscribe("destroy-test", [&](const auto &) {
        destroying.set_value();
        hand.reset();
        destroyed.set_value();
    });
    core->publish("destroy-test", 1, provenance);
    const bool reached = began.wait_for(1s) == std::future_status::ready;
    const auto blocked = done.wait_for(40ms);
    gate.release.set_value();
    calling.get();
    const bool finished = done.wait_for(2s) == std::future_status::ready;
    core->unsubscribe(subscription);
    REQUIRE(entered);
    REQUIRE(reached);
    REQUIRE(blocked == std::future_status::timeout);
    REQUIRE(finished);
}

TEST_CASE("comm: L6 composite admission uses the public timeout type and one budget", "[comm][l6][completion]") {
    auto fake = std::make_unique<can::FakeTransport>();
    auto *raw = fake.get();
    l6::L6 hand(l6::Side::kLeft, std::move(fake), "fake", test_util::unique_mapping_path());
    bool tactile = false;
    SECTION("version") {}
    SECTION("five finger tactile") { tactile = true; }
    auto held = tactile ? hand.runtime()->operation("tactile.batch", runtime::Clock::now() + 2s)
                        : hand.runtime()->operation(runtime::Clock::now() + 2s);
    auto waiting = std::async(std::launch::async, [&] {
        try {
            if (tactile) {
                (void)hand.force_sensor.get_blocking(20ms);
            } else {
                (void)hand.version.get_device_info(20ms);
            }
            return false;
        } catch (const internal::DataRelayTimeout &) {
            return true;
        } catch (...) {
            return false;
        }
    });
    const auto ready = waiting.wait_for(500ms);
    if (ready != std::future_status::ready) {
        hand.runtime()->cancel_all();
    }
    // The operations have finite admission deadlines even while the owner stays held.
    REQUIRE(waiting.get());
    REQUIRE(ready == std::future_status::ready);
    REQUIRE(raw->sent().empty());
    REQUIRE(hand.runtime()->runtime_health().in_flight == 0);
}

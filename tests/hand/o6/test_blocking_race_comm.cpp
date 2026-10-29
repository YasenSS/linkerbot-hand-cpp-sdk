#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/o6/o6.hpp>

#include "hand/support/temp_path.hpp"

namespace {
    class EarlyReplyTransport : public linkerbot::can::FakeTransport {
    public:
        std::function<bool()> received;

        void write(const linkerbot::can::Message &message) override {
            FakeTransport::write(message);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
            // Force parsing to finish before send returns, exposing send/wait gaps.
            while (!received()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    throw std::runtime_error("early reply was not parsed");
                }
                std::this_thread::yield();
            }
        }
    };
}

// 强制回包在 send 返回前解析，覆盖每个 O6 阻塞查询的注册窗口。
TEST_CASE("comm: O6 blocking getters retain replies received inside send", "[comm][o6][race]") {
    using namespace linkerbot::hand::o6;
    using linkerbot::can::Message;
    auto transport = std::make_unique<EarlyReplyTransport>();
    auto *raw = transport.get();
    raw->on_request(0x28, [](const Message &request) {
        std::vector<Message> replies;
        const bool tactile = request.dlc == 2;
        for (int row = 0; row < (tactile ? 10 : 1); ++row) {
            Message reply;
            reply.id = request.id;
            reply.dlc = tactile ? 6 : 7;
            reply.data[0] = request.data[0];
            reply.data[1] = tactile ? static_cast<uint8_t>(row << 4) : 1;
            replies.push_back(reply);
        }
        return replies;
    });
    O6 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    constexpr auto timeout = std::chrono::milliseconds{500};

#define CHECK_QUERY(manager, get, snapshot)                                                                            \
    raw->received = [&]() { return (manager).snapshot().has_value(); };                                                \
    REQUIRE_NOTHROW((manager).get(timeout))

    SECTION("angle") { CHECK_QUERY(hand.angle, get_blocking, get_snapshot); }
    SECTION("speed") { CHECK_QUERY(hand.speed, get_blocking, get_snapshot); }
    SECTION("current") { CHECK_QUERY(hand.current, get_blocking, get_snapshot); }
    SECTION("acceleration") { CHECK_QUERY(hand.acceleration, get_blocking, get_snapshot); }
    SECTION("temperature") { CHECK_QUERY(hand.temperature, get_blocking, get_snapshot); }
    SECTION("fault") { CHECK_QUERY(hand.fault, get_blocking, get_snapshot); }
    SECTION("stall time") { CHECK_QUERY(hand.stall, get_times_blocking, get_time_snapshot); }
    SECTION("stall threshold") { CHECK_QUERY(hand.stall, get_thresholds_blocking, get_threshold_snapshot); }
    SECTION("stall torque") { CHECK_QUERY(hand.stall, get_torques_blocking, get_torque_snapshot); }
    SECTION("tactile") { CHECK_QUERY(hand.force_sensor.get_finger("thumb"), get_blocking, get_snapshot); }
#undef CHECK_QUERY
}

#include "legacy_regressions.hpp"
TEST_CASE("comm: O6 tactile request boundaries", "[comm][o6][rfix]") {
    classic_regression::force_windows<linkerbot::hand::o6::SingleForceSensorManager>(10, 4);
}
TEST_CASE("comm: O6 admission budget and stop cancellation", "[comm][o6][rfix]") {
    classic_regression::lifecycle_and_budget<linkerbot::hand::o6::O6>(linkerbot::hand::o6::Side::kLeft);
}
TEST_CASE("comm: O6 interruptible polling and validation", "[comm][o6][rfix]") {
    classic_regression::polling_stop<linkerbot::hand::o6::O6>(linkerbot::hand::o6::Side::kLeft,
                                                              linkerbot::hand::o6::SensorSource::kSpeed);
}
TEST_CASE("comm: O6 stop invalidates already copied manager callback", "[comm][o6][rfix]") {
    classic_regression::copied_callback<linkerbot::hand::o6::SpeedManager>();
}
TEST_CASE("comm: O6 send receipt observes budget and stop", "[comm][o6][rfix]") {
    classic_regression::send_budget<linkerbot::hand::o6::O6>(linkerbot::hand::o6::Side::kLeft);
}
TEST_CASE("comm: O6 B3 lifecycle fixes retained", "[comm][o6][rfix]") {
    classic_regression::retained_lifecycle<linkerbot::hand::o6::O6>(linkerbot::hand::o6::Side::kLeft,
                                                                    linkerbot::hand::o6::SensorSource::kSpeed);
}
TEST_CASE("comm: O6 serial assembly and composite deadline", "[comm][o6][rfix]") {
    classic_regression::version_window<linkerbot::hand::o6::O6>(linkerbot::hand::o6::Side::kLeft, 4, 1);
}

#include "followup_regressions.hpp"
TEST_CASE("comm: O6 repeated stop barriers and callback lifecycle rejection", "[comm][o6][followup]") {
    classic_regression::repeated_stop<linkerbot::hand::o6::SingleForceSensorManager,
                                      linkerbot::hand::o6::ManagerTestAccess>(10, 4);
}
TEST_CASE("comm: O6 force complete batches survive second failure", "[comm][o6][followup]") {
    classic_regression::full_force_batches<linkerbot::hand::o6::ForceSensorManager,
                                           linkerbot::hand::o6::ManagerTestAccess>(10, 4, false);
}
TEST_CASE("comm: O6 single finger cannot enter five finger operation", "[comm][o6][followup]") {
    classic_regression::single_during_batch<linkerbot::hand::o6::ForceSensorManager>(10, 4, false);
}
TEST_CASE("comm: O6 angle stop barrier restart and setter rejection", "[comm][o6][followup]") {
    using namespace linkerbot::hand::o6;
    using namespace std::chrono_literals;
    auto transport = std::make_unique<linkerbot::can::FakeTransport>();
    auto *raw = transport.get();
    linkerbot::can::CANDispatcher dispatcher(std::move(transport));
    AngleManager manager(0x28, dispatcher, test_util::unique_mapping_path());
    manager.start();
    std::promise<void> entered, attempt, checked, release;
    auto attempted = attempt.get_future().share(), released = release.get_future().share();
    std::atomic<bool> start_rejected{false}, stop_rejected{false};
    ManagerTestAccess::sink(manager, [&](const AngleData &) {
        // Self-removal must retain a cancellation tombstone for external stop.
        ManagerTestAccess::sink(manager, nullptr);
        entered.set_value();
        attempted.wait();
        try {
            manager.start();
        } catch (const linkerbot::hand::LifecycleError &) {
            start_rejected = true;
        }
        try {
            manager.stop();
        } catch (const linkerbot::hand::LifecycleError &) {
            stop_rejected = true;
        }
        checked.set_value();
        released.wait();
    });
    linkerbot::can::Message reply;
    reply.id = 0x28;
    reply.dlc = 7;
    reply.data[0] = 1;
    raw->inject(reply);
    const auto reached = entered.get_future().wait_for(500ms);
    auto stop1 = std::async(std::launch::async, [&] { manager.stop(); });
    const auto first_wait = stop1.wait_for(20ms);
    auto stop2 = std::async(std::launch::async, [&] { manager.stop(); });
    const auto second_wait = stop2.wait_for(20ms);
    attempt.set_value();
    const auto callback_finished = checked.get_future().wait_for(500ms);
    release.set_value();
    stop1.get();
    stop2.get();
    REQUIRE(reached == std::future_status::ready);
    REQUIRE(first_wait == std::future_status::timeout);
    REQUIRE(second_wait == std::future_status::timeout);
    REQUIRE(callback_finished == std::future_status::ready);
    REQUIRE(start_rejected);
    REQUIRE(stop_rejected);
    const auto sent = raw->sent().size();
    REQUIRE_THROWS(manager.set_angles({}));
    REQUIRE_THROWS(manager.set_angles_percent(Angle{}));
    REQUIRE(raw->sent().size() == sent);
    manager.start();
    REQUIRE_NOTHROW(manager.set_angles({}));
    REQUIRE(raw->sent().size() == sent + 1);
    std::promise<void> rx_checked;
    start_rejected = false;
    stop_rejected = false;
    const auto rx = dispatcher.subscribe([&](const linkerbot::can::Message &) {
        try {
            manager.start();
        } catch (const linkerbot::hand::LifecycleError &) {
            start_rejected = true;
        }
        try {
            manager.stop();
        } catch (const linkerbot::hand::LifecycleError &) {
            stop_rejected = true;
        }
        rx_checked.set_value();
    });
    reply.id = 0x29;
    raw->inject(reply);
    REQUIRE(rx_checked.get_future().wait_for(500ms) == std::future_status::ready);
    dispatcher.unsubscribe(rx);
    REQUIRE(start_rejected);
    REQUIRE(stop_rejected);
}
TEST_CASE("comm: O6 angle can be destroyed from its event sink", "[comm][o6][followup]") {
    using namespace linkerbot::hand::o6;
    using namespace std::chrono_literals;
    auto transport = std::make_unique<linkerbot::can::FakeTransport>();
    auto *raw = transport.get();
    linkerbot::can::CANDispatcher dispatcher(std::move(transport));
    // The event executor retains its Runtime until the callback returns.
    // Keep the borrowed Session/Runtime alive and close them externally before
    // their Dispatcher, as required by Session::borrow's lifetime contract.
    auto session = linkerbot::transport::Session::borrow(dispatcher);
    auto runtime = linkerbot::hand::runtime::DeviceRuntime::acquire(session, "o6/40", {{0x28, false}});
    auto manager = std::make_unique<AngleManager>(0x28, runtime, test_util::unique_mapping_path());
    manager->start();
    std::promise<void> destroyed;
    ManagerTestAccess::sink(*manager, [&](const AngleData &) {
        manager.reset();
        destroyed.set_value();
    });
    linkerbot::can::Message reply;
    reply.id = 0x28;
    reply.dlc = 7;
    reply.data[0] = 1;
    raw->inject(reply);
    REQUIRE(destroyed.get_future().wait_for(500ms) == std::future_status::ready);
    REQUIRE_FALSE(manager);
    runtime->close();
    session->close();
}

TEST_CASE("comm: O6 queued real manager calls drain before destruction", "[comm][o6][final-stop]") {
    using namespace linkerbot::hand::o6;
    using namespace std::chrono_literals;
    SECTION("Runtime current admission") {
        classic_regression::queued_destruction<CurrentManager, ManagerTestAccess>(
            [](auto &m) { (void)m.get_blocking(5s); }, 3);
    }
    SECTION("Runtime version composite admission") {
        classic_regression::queued_destruction<VersionManager, ManagerTestAccess>(
            [](auto &m) { (void)m.get_device_info(5s); }, 3);
    }
    SECTION("Runtime five finger admission") {
        classic_regression::queued_destruction<ForceSensorManager, ManagerTestAccess>(
            [](auto &m) { (void)m.get_blocking(5s); }, 3, 5);
    }
}
TEST_CASE("comm: O6 stop cancels current before waiting for angle sink", "[comm][o6][final-stop]") {
    using namespace linkerbot::hand::o6;
    classic_regression::cancel_all_before_angle_barrier<O6, Side, ManagerTestAccess>(Side::kLeft, false);
}
TEST_CASE("comm: O6 force stop cancels every finger before any sink barrier", "[comm][o6][final-stop]") {
    using namespace linkerbot::hand::o6;
    classic_regression::cancel_all_fingers_before_barrier<ForceSensorManager, ManagerTestAccess>(10, 4);
}
TEST_CASE("comm: O6 single finger stop cancels shared admission", "[comm][o6][final-stop]") {
    using namespace linkerbot::hand::o6;
    classic_regression::stop_single_queued_on_shared_tactile_gate<ForceSensorManager, ManagerTestAccess>();
}

#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <thread>
#include <variant>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l20lite/events.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l20lite::ForceSensorEvent;
using linkerbot::hand::l20lite::L20lite;
using linkerbot::hand::l20lite::SensorEvent;
using linkerbot::hand::l20lite::SensorSource;
using linkerbot::hand::l20lite::Side;

namespace {

    constexpr uint8_t kFill = 0x11;
    constexpr uint8_t kRequestByte = 0xC6;
    constexpr auto kTimeout = std::chrono::milliseconds{500};

    std::vector<Message> force_frames(uint32_t id, uint8_t prefix, uint8_t fill) {
        std::vector<Message> frames;
        frames.reserve(12);
        for (uint8_t i = 0; i < 12; ++i) {
            Message msg;
            msg.id = id;
            msg.dlc = 8;
            msg.data[0] = prefix;
            msg.data[1] = static_cast<uint8_t>(i << 4);
            for (uint8_t j = 0; j < 6; ++j) {
                msg.data[2 + j] = fill;
            }
            frames.push_back(msg);
        }
        return frames;
    }

    FakeTransport::Handler force_handler(uint32_t id) {
        return [id](const Message &req) {
            if (req.dlc < 2 || req.data[1] != kRequestByte) {
                return std::vector<Message>{};
            }
            if (req.data[0] < 0xB1 || req.data[0] > 0xB5) {
                return std::vector<Message>{};
            }
            return force_frames(id, req.data[0], kFill);
        };
    }

    std::vector<SensorEvent> collect_events(L20lite &hand, std::chrono::milliseconds duration) {
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

} // namespace

// 点阵：BYTE1=0xC6 表示 12 行 × 6 列（与 L6 相同，不是 O6 的 0xA4 10×4）。

// 轮询应向五指各发一帧：前缀 0xB1–0xB5，BYTE1=0xC6。
TEST_CASE("comm: force sensor polling sends all finger requests", "[comm][l20lite][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20lite_comm::kLeftId, force_handler(l20lite_comm::kLeftId));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    raw->clear_sent();

    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{40});
    std::this_thread::sleep_for(std::chrono::milliseconds{120});
    hand.stop_polling();

    std::set<uint8_t> prefixes;
    for (const auto &msg : raw->sent()) {
        if (msg.dlc == 2 && msg.data[1] == kRequestByte) {
            prefixes.insert(msg.data[0]);
        }
    }
    REQUIRE(prefixes == std::set<uint8_t>{0xB1, 0xB2, 0xB3, 0xB4, 0xB5});
}

// 轮询后快照有点阵；拇指 [0][0]、小指 [11][5] 都应是填充值。
TEST_CASE("comm: force sensor polling populates snapshot", "[comm][l20lite][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId, force_handler(l20lite_comm::kLeftId));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{40});
    std::this_thread::sleep_for(kTimeout);
    hand.stop_polling();

    const auto snap = hand.get_snapshot();
    REQUIRE(snap.force_sensor.has_value());
    REQUIRE(test_support::require_value(snap.force_sensor).thumb.values[0][0] == kFill);
    REQUIRE(test_support::require_value(snap.force_sensor).pinky.values[11][5] == kFill);
}

// 轮询期间数据流应吐出 ForceSensorEvent，食指 [0][0] 为填充值。
TEST_CASE("comm: force sensor stream emits ForceSensorEvent", "[comm][l20lite][force_sensor]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(l20lite_comm::kLeftId, force_handler(l20lite_comm::kLeftId));

    L20lite hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.start_polling({SensorSource::kForceSensor}, std::chrono::milliseconds{40});

    const auto events = collect_events(hand, kTimeout);
    hand.stop_polling();

    REQUIRE_FALSE(events.empty());
    bool has_force = false;
    for (const auto &event : events) {
        if (std::holds_alternative<ForceSensorEvent>(event)) {
            has_force = true;
            REQUIRE(std::get<ForceSensorEvent>(event).data.index.values[0][0] == kFill);
        }
    }
    REQUIRE(has_force);
}

#include "hand/o6/legacy_regressions.hpp"
TEST_CASE("comm: L20Lite tactile request boundaries", "[comm][l20lite][rfix]") {
    classic_regression::force_windows<linkerbot::hand::l20lite::SingleForceSensorManager>(12, 6);
}
TEST_CASE("comm: L20Lite admission budget and stop cancellation", "[comm][l20lite][rfix]") {
    classic_regression::lifecycle_and_budget<L20lite>(Side::kLeft);
}
TEST_CASE("comm: L20Lite interruptible polling and validation", "[comm][l20lite][rfix]") {
    classic_regression::polling_stop<L20lite>(Side::kLeft, SensorSource::kSpeed, 2);
}
TEST_CASE("comm: L20Lite stop invalidates copied manager callback", "[comm][l20lite][rfix]") {
    classic_regression::copied_callback<linkerbot::hand::l20lite::SpeedManager>();
}
TEST_CASE("comm: L20Lite send receipt observes budget and stop", "[comm][l20lite][rfix]") {
    classic_regression::send_budget<L20lite>(Side::kLeft);
}
TEST_CASE("comm: L20Lite B3 lifecycle fixes retained", "[comm][l20lite][rfix]") {
    classic_regression::retained_lifecycle<L20lite>(Side::kLeft, SensorSource::kSpeed);
}
TEST_CASE("comm: L20Lite serial assembly and composite deadline", "[comm][l20lite][rfix]") {
    classic_regression::version_window<L20lite>(Side::kLeft, 3, 6);
}

#include "hand/o6/followup_regressions.hpp"
TEST_CASE("comm: L20Lite repeated stop barriers and callback lifecycle rejection", "[comm][l20lite][followup]") {
    classic_regression::repeated_stop<linkerbot::hand::l20lite::SingleForceSensorManager,
                                      linkerbot::hand::l20lite::ManagerTestAccess>(12, 6);
}
TEST_CASE("comm: L20Lite force complete batches survive second failure", "[comm][l20lite][followup]") {
    classic_regression::full_force_batches<linkerbot::hand::l20lite::ForceSensorManager,
                                           linkerbot::hand::l20lite::ManagerTestAccess>(12, 6);
}
TEST_CASE("comm: L20Lite single finger cannot enter five finger operation", "[comm][l20lite][followup]") {
    classic_regression::single_during_batch<linkerbot::hand::l20lite::ForceSensorManager>(12, 6, true);
}

TEST_CASE("comm: L20Lite queued real manager calls drain before destruction", "[comm][l20lite][final-stop]") {
    using namespace linkerbot::hand::l20lite;
    using namespace std::chrono_literals;
    SECTION("Runtime current admission") {
        classic_regression::queued_destruction<CurrentManager, ManagerTestAccess>(
            [](auto &m) { (void)m.get_blocking(5s); }, 3);
    }
    SECTION("Runtime version admission") {
        classic_regression::queued_destruction<VersionManager, ManagerTestAccess>(
            [](auto &m) { (void)m.get_device_info(5s); }, 3);
    }
    SECTION("Runtime five finger admission") {
        classic_regression::queued_destruction<ForceSensorManager, ManagerTestAccess>(
            [](auto &m) { (void)m.get_blocking(5s); }, 3, 5);
    }
}
TEST_CASE("comm: L20Lite stop cancels current before waiting for angle sink", "[comm][l20lite][final-stop]") {
    using namespace linkerbot::hand::l20lite;
    classic_regression::cancel_all_before_angle_barrier<L20lite, Side, ManagerTestAccess>(Side::kLeft, true);
}
TEST_CASE("comm: L20Lite force stop cancels every finger before any sink barrier", "[comm][l20lite][final-stop]") {
    using namespace linkerbot::hand::l20lite;
    classic_regression::cancel_all_fingers_before_barrier<ForceSensorManager, ManagerTestAccess>(12, 6);
}
TEST_CASE("comm: L20Lite single finger stop cancels shared admission", "[comm][l20lite][final-stop]") {
    using namespace linkerbot::hand::l20lite;
    classic_regression::stop_single_queued_on_shared_tactile_gate<ForceSensorManager, ManagerTestAccess>();
}

#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l6/frame.hpp>
#include <linkerbot/hand/l6/l6.hpp>
#include <linkerbot/hand/l6/runtime.hpp>
#include <linkerbot/relay.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l6::Angle;
using linkerbot::hand::l6::AngleFeedbackPolicy;
using linkerbot::hand::l6::L6;
using linkerbot::hand::l6::L6PollingPlan;
using linkerbot::hand::l6::SampleOrigin;
using linkerbot::hand::l6::Side;

namespace {
    constexpr uint32_t kLeftId = linkerbot::hand::l6::kLeftArbitrationId;
    constexpr auto kTimeout = std::chrono::milliseconds{80};

    template <typename Predicate> void wait_for_stats(Predicate ready) {
        // Relay notification precedes asynchronous statistics publication.
        // Wait for the observed condition; never assume the receiver has finished.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
        while (!ready() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    Message angle_reply(uint8_t fill) {
        Message reply;
        reply.id = kLeftId;
        reply.dlc = 7;
        reply.data[0] = linkerbot::hand::l6::kAngleOpcode;
        for (size_t i = 0; i < Angle::kJointCount; ++i) {
            reply.data[i + 1] = fill;
        }
        return reply;
    }

    Message tactile_frame(uint8_t opcode, uint8_t row, uint8_t fill) {
        Message reply;
        reply.id = kLeftId;
        reply.dlc = 8;
        reply.data[0] = opcode;
        reply.data[1] = row;
        for (uint8_t column = 0; column < 6; ++column) {
            reply.data[column + 2] = fill;
        }
        return reply;
    }

    std::vector<Message> tactile_frames(uint8_t opcode, uint8_t fill) {
        std::vector<Message> frames;
        frames.reserve(12);
        for (uint8_t row = 0; row < 12; ++row) {
            frames.push_back(tactile_frame(opcode, row, fill));
        }
        return frames;
    }

    FakeTransport::Handler echo_handler(std::atomic<int> *in_flight = nullptr,
                                        std::atomic<int> *max_in_flight = nullptr) {
        return [in_flight, max_in_flight](const Message &request) {
            if (in_flight != nullptr) {
                const int current = ++(*in_flight);
                if (max_in_flight != nullptr) {
                    int prev = max_in_flight->load();
                    while (prev < current && !max_in_flight->compare_exchange_weak(prev, current)) {
                    }
                }
            }
            std::vector<Message> replies;
            if (request.dlc >= 1 && request.data[0] == linkerbot::hand::l6::kAngleOpcode) {
                replies.push_back(angle_reply(request.dlc == 1 ? 0x40 : request.data[1]));
            } else if (request.dlc == 2 && request.data[1] == 0xC6 && request.data[0] >= 0xB1 &&
                       request.data[0] <= 0xB5) {
                replies = tactile_frames(request.data[0], 0x21);
            }
            if (in_flight != nullptr) {
                --(*in_flight);
            }
            return replies;
        };
    }

    size_t count_opcode(const std::vector<Message> &sent, uint8_t opcode, uint8_t dlc) {
        size_t count = 0;
        for (const auto &msg : sent) {
            if (msg.dlc == dlc && msg.data[0] == opcode) {
                ++count;
            }
        }
        return count;
    }
} // namespace

// 发送前先注册 waiter，同步回包不能把下一次查询的等待者提前完成。
TEST_CASE("comm: L6 keeps an immediately available angle reply", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, echo_handler());
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    const auto sample = hand.angle.get_blocking(kTimeout);
    REQUIRE(sample.origin == SampleOrigin::Query);
    REQUIRE(sample.generation > 0);
    REQUIRE(sample.sequence > 0);
    REQUIRE(sample.device_raw == std::array<uint8_t, Angle::kJointCount>{0x40, 0x40, 0x40, 0x40, 0x40, 0x40});
    wait_for_stats([&]() {
        const auto stats = hand.get_runtime_stats();
        return stats.query_angle_count >= 1 && stats.pending_count == 0 && stats.in_flight_count == 0;
    });
    const auto stats = hand.get_runtime_stats();
    REQUIRE(stats.pending_count == 0);
    REQUIRE(stats.in_flight_count == 0);
    REQUIRE(stats.query_angle_count >= 1);
}

// 命令回包在 0xCC 打开后标记为 CommandFeedback，而不是 Query。
TEST_CASE("comm: L6 tags command replies as CommandFeedback", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, echo_handler());
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.config.set_reply_enabled(true);
    const auto sample = hand.angle.set_angles_percent_blocking(Angle(10, 20, 30, 40, 50, 60), kTimeout);
    REQUIRE(sample.origin == SampleOrigin::CommandFeedback);
    wait_for_stats([&]() { return hand.get_runtime_stats().command_feedback_angle_count >= 1; });
    REQUIRE(hand.get_runtime_stats().command_feedback_angle_count >= 1);
    REQUIRE(hand.get_runtime_stats().unknown_origin_count == 0);
}

// 查询与命令不能同时占用角度在途事务。
TEST_CASE("comm: L6 serializes query and command angle transactions", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    std::atomic<int> in_flight{0};
    std::atomic<int> max_in_flight{0};
    transport->on_request(kLeftId, echo_handler(&in_flight, &max_in_flight));
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.config.set_reply_enabled(true);

    std::thread query([&] { (void)hand.angle.get_blocking(kTimeout); });
    std::thread command([&] { (void)hand.angle.set_angles_percent_blocking(Angle(40, 40, 40, 40, 40, 40), kTimeout); });
    query.join();
    command.join();
    REQUIRE(max_in_flight.load() == 1);
    REQUIRE(hand.get_runtime_stats().in_flight_count == 0);
    REQUIRE(hand.get_runtime_stats().pending_count == 0);
}

// 超时后 waiter 和 in-flight 都被清掉。
TEST_CASE("comm: L6 angle timeout clears waiter and in-flight", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, [](const Message &) { return std::vector<Message>{}; });
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    REQUIRE_THROWS_AS(hand.angle.get_blocking(kTimeout), std::runtime_error);
    const auto stats = hand.get_runtime_stats();
    REQUIRE(stats.pending_count == 0);
    REQUIRE(stats.in_flight_count == 0);
    REQUIRE(stats.timeout_count >= 1);
}

// 超时事务的迟到帧不能完成下一笔查询。
TEST_CASE("comm: L6 late angle frames cannot complete the next query", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::atomic<int> queries{0};
    raw->on_request(kLeftId, [&](const Message &request) {
        if (request.dlc != 1 || request.data[0] != 0x01) {
            return std::vector<Message>{};
        }
        if (++queries == 1) {
            return std::vector<Message>{};
        }
        return std::vector<Message>{angle_reply(0x22)};
    });

    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    REQUIRE_THROWS_AS(hand.angle.get_blocking(kTimeout), std::runtime_error);
    raw->inject(angle_reply(0x11));
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    const auto sample = hand.angle.get_blocking(kTimeout);
    REQUIRE(sample.origin == SampleOrigin::Query);
    REQUIRE(sample.angle.thumb_flex > 13.0F);
    REQUIRE(sample.angle.thumb_flex < 14.0F);
    REQUIRE(hand.get_runtime_stats().late_response_count >= 1);
}

// CommandFeedbackOnly 在 0xCC 未打开时立即失败。
TEST_CASE("comm: L6 CommandFeedbackOnly requires the reply switch", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, echo_handler());
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.angle_enabled = true;
    plan.angle_period = std::chrono::microseconds{20'000};
    plan.angle_policy = AngleFeedbackPolicy::CommandFeedbackOnly;
    REQUIRE_THROWS_AS(hand.start_polling(plan), std::runtime_error);
}

// adaptive 在命令反馈足够时不再按目标频率发 query。
TEST_CASE("comm: L6 adaptive uses command feedback instead of extra queries", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, echo_handler());
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    hand.config.set_reply_enabled(true);
    raw->clear_sent();

    L6PollingPlan plan;
    plan.angle_enabled = true;
    plan.angle_period = std::chrono::microseconds{20'000};
    plan.angle_policy = AngleFeedbackPolicy::Adaptive;
    plan.request_timeout = std::chrono::milliseconds{80};
    hand.start_polling(plan);
    for (int i = 0; i < 20; ++i) {
        (void)hand.angle.set_angles_percent_blocking(Angle(50, 50, 50, 50, 50, 50), kTimeout);
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    hand.stop_polling();
    const auto sent = raw->sent();
    const auto queries = count_opcode(sent, 0x01, 1);
    const auto commands = count_opcode(sent, 0x01, 7);
    REQUIRE(commands >= 20);
    REQUIRE(queries < commands);
    REQUIRE(hand.get_runtime_stats().command_feedback_angle_count >= 20);
}

// adaptive 在没有命令反馈时恢复主动查询。
TEST_CASE("comm: L6 adaptive resumes queries when commands stop", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, echo_handler());
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    raw->clear_sent();
    L6PollingPlan plan;
    plan.angle_enabled = true;
    plan.angle_period = std::chrono::microseconds{10'000};
    plan.angle_policy = AngleFeedbackPolicy::Adaptive;
    plan.request_timeout = std::chrono::milliseconds{80};
    hand.start_polling(plan);
    std::this_thread::sleep_for(std::chrono::milliseconds{80});
    hand.stop_polling();
    REQUIRE(count_opcode(raw->sent(), 0x01, 1) >= 3);
    REQUIRE(hand.get_runtime_stats().query_angle_count >= 3);
}

// 过期 deadline 只跳过积压周期，不补发。
TEST_CASE("comm: L6 scheduler skips overdue periods instead of bursting", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, [](const Message &) { return std::vector<Message>{}; });
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.angle_enabled = true;
    plan.angle_period = std::chrono::microseconds{5'000};
    plan.request_timeout = std::chrono::milliseconds{40};
    hand.start_polling(plan);
    // Worker startup and the first timeout need not fit inside a 50ms sleep.
    wait_for_stats([&] { return hand.get_runtime_stats().skipped_deadline_count >= 1; });
    hand.stop_polling();
    REQUIRE(hand.get_runtime_stats().skipped_deadline_count >= 1);
    REQUIRE(count_opcode(raw->sent(), 0x01, 1) < 8);
}

// 停止调度后不再发送。
TEST_CASE("comm: L6 stop_polling cancels further sends", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kLeftId, echo_handler());
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.angle_enabled = true;
    plan.angle_period = std::chrono::microseconds{5'000};
    hand.start_polling(plan);
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    hand.stop_polling();
    raw->clear_sent();
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    REQUIRE(raw->sent().empty());
    REQUIRE(hand.get_runtime_stats().pending_count == 0);
    REQUIRE(hand.get_runtime_stats().in_flight_count == 0);
}

// 相同触觉行可接受。
TEST_CASE("comm: L6 tactile accepts identical duplicate rows", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, [](const Message &request) {
        if (request.dlc != 2 || request.data[0] != 0xB1) {
            return std::vector<Message>{};
        }
        auto frames = tactile_frames(0xB1, 0x07);
        frames.insert(frames.begin() + 1, tactile_frame(0xB1, 0, 0x07));
        return frames;
    });
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    const auto sample = hand.force_sensor.get_finger("thumb").get_blocking(kTimeout);
    REQUIRE(sample.values[0][0] == 0x07);
    REQUIRE(hand.get_runtime_stats().duplicate_count >= 1);
    REQUIRE(sample.generation > 0);
}

// 五指中任一手指失败导致整批失败。
TEST_CASE("comm: L6 five-finger tactile fails if any finger fails", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, [](const Message &request) {
        if (request.dlc != 2 || request.data[1] != 0xC6) {
            return std::vector<Message>{};
        }
        if (request.data[0] == 0xB5) {
            return std::vector<Message>{};
        }
        return tactile_frames(request.data[0], 0x09);
    });
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    REQUIRE_THROWS_AS(hand.force_sensor.get_blocking(kTimeout), std::runtime_error);
    REQUIRE_FALSE(hand.force_sensor.get_snapshot().has_value());
    const auto stats = hand.get_runtime_stats();
    REQUIRE(stats.tactile_timeout_count == 1);
    REQUIRE(stats.tactile_finger_timeout_count[4] == 1);
    for (size_t finger = 0; finger < 4; ++finger) {
        REQUIRE(stats.tactile_finger_timeout_count[finger] == 0);
    }
}

// 不完整的历史手指缓存不能组成完整样本。
TEST_CASE("comm: L6 does not stitch historical fingers into a new sample", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    std::atomic<int> rounds{0};
    transport->on_request(kLeftId, [&](const Message &request) {
        if (request.dlc != 2 || request.data[1] != 0xC6) {
            return std::vector<Message>{};
        }
        const int round = rounds.load();
        if (round >= 5 && request.data[0] != 0xB1) {
            return std::vector<Message>{};
        }
        ++rounds;
        return tactile_frames(request.data[0], static_cast<uint8_t>(round + 1));
    });
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    const auto first = hand.force_sensor.get_blocking(kTimeout);
    REQUIRE_THROWS_AS(hand.force_sensor.get_blocking(kTimeout), std::runtime_error);
    const auto snap = hand.force_sensor.get_snapshot();
    REQUIRE(snap.has_value());
    REQUIRE(snap->sequence == first.sequence);
    REQUIRE(snap->generation == first.generation);
    REQUIRE(snap->thumb.values[0][0] == first.thumb.values[0][0]);
}

// sequence 与 generation 单调且归属正确。
TEST_CASE("comm: L6 angle generation belongs to the in-flight transaction", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, echo_handler());
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    const auto first = hand.angle.get_blocking(kTimeout);
    const auto second = hand.angle.get_blocking(kTimeout);
    REQUIRE(second.sequence == first.sequence + 1);
    REQUIRE(second.generation > first.generation);
    REQUIRE(first.origin == SampleOrigin::Query);
    REQUIRE(second.origin == SampleOrigin::Query);
}

// 队列满时 event_drop_count 增长。
TEST_CASE("comm: L6 event queue drop_count grows when full", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, echo_handler());
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    auto &queue = hand.stream(1);
    (void)queue;
    for (int i = 0; i < 8; ++i) {
        (void)hand.angle.get_blocking(kTimeout);
    }
    REQUIRE(hand.get_runtime_stats().event_drop_count >= 1);
    hand.stop_stream();
}

// 错误帧类型、DLC、CAN ID 被拒绝。
TEST_CASE("comm: L6 rejects mismatched angle frame type and DLC", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, [](const Message &request) {
        if (request.dlc != 1 || request.data[0] != 0x01) {
            return std::vector<Message>{};
        }
        auto wrong_id = angle_reply(0x10);
        wrong_id.id = 0x27;
        auto fd = angle_reply(0x20);
        fd.is_fd = true;
        auto brs = angle_reply(0x21);
        brs.is_brs = true;
        auto long_frame = angle_reply(0x30);
        long_frame.dlc = 8;
        return std::vector<Message>{wrong_id, fd, brs, long_frame, angle_reply(0x40)};
    });
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    const auto sample = hand.angle.get_blocking(kTimeout);
    REQUIRE(sample.angle.thumb_flex > 25.0F);
    REQUIRE(sample.angle.thumb_flex < 26.0F);
    REQUIRE(hand.get_runtime_stats().protocol_error_count >= 1);
}

// 析构和取消期间不发生死锁。
TEST_CASE("comm: L6 destructor cancels polling without deadlock", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, echo_handler());
    {
        L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
        L6PollingPlan plan;
        plan.angle_enabled = true;
        plan.tactile_enabled = true;
        plan.angle_period = std::chrono::microseconds{5'000};
        plan.tactile_period = std::chrono::microseconds{20'000};
        hand.start_polling(plan);
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    SUCCEED();
}

// 单指 12 行事务完成前，角度查询不能插入同一设备的发送窗口。
TEST_CASE("comm: L6 serializes angle traffic with one tactile finger transaction", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::atomic<bool> tactile_pending{false};
    std::atomic<bool> angle_overlapped{false};
    std::thread delayed_reply;
    raw->on_request(kLeftId, [&](const Message &request) {
        if (request.dlc == 2 && request.data[0] == 0xB1) {
            tactile_pending.store(true);
            delayed_reply = std::thread([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
                for (const auto &frame : tactile_frames(0xB1, 0x31)) {
                    raw->inject(frame);
                }
                tactile_pending.store(false);
            });
            return std::vector<Message>{};
        }
        if (request.dlc == 1 && request.data[0] == 0x01) {
            if (tactile_pending.load()) {
                angle_overlapped.store(true);
            }
            return std::vector<Message>{angle_reply(0x42)};
        }
        return std::vector<Message>{};
    });
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    std::thread tactile(
        [&] { (void)hand.force_sensor.get_finger("thumb").get_blocking(std::chrono::milliseconds{100}); });
    while (!tactile_pending.load()) {
        std::this_thread::yield();
    }
    const auto angle = hand.angle.get_blocking(std::chrono::milliseconds{100});
    tactile.join();
    if (delayed_reply.joinable()) {
        delayed_reply.join();
    }

    REQUIRE_FALSE(angle_overlapped.load());
    REQUIRE(angle.origin == SampleOrigin::Query);
}

// 活跃触觉事务遇到非法 DLC 时应立即失败，而不是等到请求超时。
TEST_CASE("comm: L6 tactile protocol errors fail the active transaction", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, [](const Message &request) {
        if (request.dlc != 2 || request.data[0] != 0xB1) {
            return std::vector<Message>{};
        }
        auto invalid = tactile_frame(0xB1, 0, 0x11);
        invalid.dlc = 7;
        return std::vector<Message>{invalid};
    });
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    const auto started = std::chrono::steady_clock::now();
    REQUIRE_THROWS_AS(hand.force_sensor.get_finger("thumb").get_blocking(std::chrono::milliseconds{200}),
                      std::runtime_error);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE(elapsed < std::chrono::milliseconds{100});
    const auto stats = hand.get_runtime_stats();
    REQUIRE(stats.protocol_error_count == 1);
    REQUIRE(stats.pending_count == 0);
    REQUIRE(stats.in_flight_count == 0);
}

// 冲突失败后的旧行必须在隔离窗内丢弃，不能污染下一次同手指事务。
TEST_CASE("comm: L6 tactile conflict quarantines late rows", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::atomic<int> requests{0};
    raw->on_request(kLeftId, [&](const Message &request) {
        if (request.dlc != 2 || request.data[0] != 0xB1) {
            return std::vector<Message>{};
        }
        if (++requests == 1) {
            return std::vector<Message>{tactile_frame(0xB1, 0, 0x11), tactile_frame(0xB1, 0, 0x22)};
        }
        return tactile_frames(0xB1, 0x44);
    });
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    REQUIRE_THROWS_AS(hand.force_sensor.get_finger("thumb").get_blocking(kTimeout), std::runtime_error);
    raw->inject(tactile_frame(0xB1, 1, 0x33));
    const auto started = std::chrono::steady_clock::now();
    const auto sample = hand.force_sensor.get_finger("thumb").get_blocking(kTimeout);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(sample.values[1][0] == 0x44);
    REQUIRE(elapsed >= std::chrono::milliseconds{3});
    REQUIRE(hand.get_runtime_stats().late_response_count >= 1);
    REQUIRE(hand.get_runtime_stats().pending_count == 0);
    REQUIRE(hand.get_runtime_stats().in_flight_count == 0);
}

// 运行时统计必须分别提供角度/触觉完成数和非零延迟。
TEST_CASE("comm: L6 runtime exposes per-channel completion and latency", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kLeftId, echo_handler());
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    (void)hand.angle.get_blocking(kTimeout);
    (void)hand.force_sensor.get_blocking(std::chrono::milliseconds{200});
    const auto stats = hand.get_runtime_stats();
    REQUIRE(stats.angle_completed_count == 1);
    REQUIRE(stats.tactile_completed_count == 1);
    REQUIRE(stats.angle_latency.count == 1);
    REQUIRE(stats.tactile_latency.count == 1);
}

// 零容量事件队列会导致 pop_front 空容器，必须在构造时拒绝。
TEST_CASE("comm: EventQueue rejects zero capacity", "[comm][l6][runtime]") {
    REQUIRE_THROWS_AS((linkerbot::EventQueue<int>{0}), std::invalid_argument);
}

// stop_polling 必须取消非角度 manager 的 waiter，不能等待完整请求超时。
TEST_CASE("comm: L6 stop_polling cancels all manager waiters", "[comm][l6][runtime]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    transport->on_request(kLeftId, [](const Message &) { return std::vector<Message>{}; });
    L6 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.torque_enabled = true;
    plan.torque_period = std::chrono::microseconds{10'000};
    plan.request_timeout = std::chrono::seconds{2};
    hand.start_polling(plan);
    while (count_opcode(raw->sent(), 0x03, 1) == 0) {
        std::this_thread::yield();
    }
    const auto started = std::chrono::steady_clock::now();
    hand.stop_polling();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE(elapsed < std::chrono::milliseconds{100});
    REQUIRE(hand.get_runtime_stats().pending_count == 0);
    REQUIRE(hand.get_runtime_stats().in_flight_count == 0);
}

// deadline 与响应竞争时，只能二选一：成功认领 waiter 或超时，不得同时发生。
TEST_CASE("comm: DataRelay timeout and delivery are atomically arbitrated", "[comm][l6][runtime]") {
    for (int iteration = 0; iteration < 50; ++iteration) {
        linkerbot::internal::DataRelay<int> relay;
        std::atomic<bool> request_sent{false};
        std::atomic<bool> returned{false};
        std::atomic<bool> timed_out{false};
        std::thread requester([&] {
            try {
                const auto value = relay.request([&] { request_sent.store(true); }, 0.002F);
                returned.store(value == 42);
            } catch (const std::runtime_error &) {
                timed_out.store(true);
            }
        });
        while (!request_sent.load()) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
        const bool accepted = relay.push_if_waiting(42);
        requester.join();

        REQUIRE(returned.load() != timed_out.load());
        REQUIRE(accepted == returned.load());
        REQUIRE(relay.snapshot().has_value() == accepted);
        REQUIRE(relay.pending_count() == 0);
    }
}

TEST_CASE("comm: L6 whole hands share a session and coordinate recovery", "[comm][l6][runtime]") {
    using namespace std::chrono_literals;
    unsigned opens = 0;
    auto session = linkerbot::transport::Session::create([&] {
        ++opens;
        auto fake = std::make_unique<FakeTransport>();
        fake->echo_cmd(0x28, 1, 6, 42);
        fake->echo_cmd(0x27, 1, 6, 51);
        return fake;
    });
    L6 left(Side::kLeft, session, "shared", test_util::unique_mapping_path());
    L6 right(Side::kRight, session, "shared", test_util::unique_mapping_path());
    (void)left.angle.get_blocking(1s);
    (void)right.angle.get_blocking(1s);
    auto handle = left.stream_handle();
    REQUIRE_THROWS_AS(left.stream(), linkerbot::hand::LifecycleError);
    left.stop_stream();
    auto &legacy = left.stream();
    left.stop_stream();
    auto next = left.stream_handle();
    REQUIRE(handle->closed());
    REQUIRE(legacy.closed());
    REQUIRE_FALSE(next->closed());
    left.recover();
    REQUIRE_FALSE(left.angle.get_snapshot().has_value());
    REQUIRE_FALSE(right.angle.get_snapshot().has_value());
    REQUIRE_THROWS(right.angle.get_blocking(1s));
    right.recover();
    REQUIRE(opens == 2);
    (void)right.angle.get_blocking(1s);
    left.stop();
    REQUIRE(next->closed());
    (void)right.angle.get_blocking(1s);
    REQUIRE(session->health().open);
    right.stop();
    session->close();
}

TEST_CASE("comm: L6 legacy external gate retains cross-device deadline exclusion", "[comm][l6][runtime]") {
    using namespace std::chrono_literals;
    auto fake = std::make_unique<FakeTransport>();
    auto *raw = fake.get();
    fake->echo_cmd(0x28, 1, 6, 42);
    linkerbot::can::CANDispatcher dispatcher(std::move(fake));
    auto gate = std::make_shared<std::timed_mutex>();
    linkerbot::hand::l6::AngleManager manager(0x28, dispatcher, test_util::unique_mapping_path(), "left", "gate", gate);
    manager.start();
    std::unique_lock<std::timed_mutex> held(*gate);
    auto blocked = std::async(std::launch::async, [&] {
        try {
            (void)manager.get_blocking(20ms);
            return false;
        } catch (const std::runtime_error &) {
            return true;
        }
    });
    const bool timed_out = blocked.get();
    const auto sent_while_held = raw->sent().size();
    held.unlock();
    REQUIRE(timed_out);
    REQUIRE(sent_while_held == 0);
    REQUIRE_NOTHROW(manager.get_blocking(1s));
}

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "hand/support/temp_path.hpp"
#include "l6_raw_session.hpp"
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l6/detail/interstage.hpp>
#include <linkerbot/hand/l6/l6.hpp>

using namespace std::chrono_literals;
using namespace linkerbot::hand::l6;
using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using Clock = std::chrono::steady_clock;

namespace {
    std::vector<Message> response(const Message &request) {
        std::vector<Message> frames;
        const bool finger = request.dlc == 2 && request.data[0] >= 0xB1 && request.data[0] <= 0xB5;
        for (unsigned row = 0; row < (finger ? 12u : 1u); ++row) {
            auto reply = request;
            reply.dlc = finger ? 8 : 7;
            reply.data[1] = static_cast<uint8_t>(row);
            frames.push_back(reply);
        }
        return frames;
    }

    template <typename Predicate> bool await(Predicate done) {
        const auto until = Clock::now() + 1s;
        while (!done() && Clock::now() < until) {
            std::this_thread::sleep_for(1ms);
        }
        return done();
    }
}

// 用可控时间检查跨两个角度节拍的风险，以及次数和触觉预留预算的限制。
TEST_CASE("comm: L6 boundary decisions preserve tactile budget and angle phase", "[comm][l6][interstage]") {
    using detail::BoundaryAction;
    const Clock::time_point start{};
    InterstageOptions options;
    options.finger_estimate = 2360us;
    options.timing_margin = 0us;
    options.angle_budget = 1ms;
    const auto decide = [&](std::chrono::microseconds now, unsigned dispatched = 0,
                            std::chrono::microseconds deadline = 33ms) {
        return detail::boundary_action(options, start + now, start + 2ms, 2ms, start + deadline, 5, dispatched);
    };
    REQUIRE(decide(1800us) == BoundaryAction::ContinueTactile);
    options.query_lookahead = true;
    REQUIRE(decide(1800us) == BoundaryAction::WaitForAngle);
    REQUIRE(decide(2ms) == BoundaryAction::QueryAngle);
    REQUIRE(decide(550us) == BoundaryAction::ContinueTactile);
    REQUIRE(decide(1800us, 4) == BoundaryAction::ContinueTactile);
    REQUIRE(decide(1800us, 0, 14ms) == BoundaryAction::BudgetExhausted);
    options.max_angle_transactions = 0;
    REQUIRE(decide(2ms) == BoundaryAction::ContinueTactile);
}

TEST_CASE("comm: L6 adaptive feedback covers only reached angle deadlines", "[comm][l6][interstage]") {
    const Clock::time_point start{};
    std::optional<Clock::time_point> feedback;

    REQUIRE(detail::adaptive_requires_query(feedback, start));

    // A command response exactly at a deadline satisfies that deadline.
    feedback = start;
    REQUIRE_FALSE(detail::adaptive_requires_query(feedback, start));

    // With 50 Hz commands and 100 Hz angle deadlines, the same response must
    // not suppress the intermediate 10 ms query indefinitely.
    REQUIRE(detail::adaptive_requires_query(feedback, start + 10ms));
    feedback = start + 20ms;
    REQUIRE_FALSE(detail::adaptive_requires_query(feedback, start + 20ms));
    REQUIRE(detail::adaptive_requires_query(feedback, start + 30ms));
}

// 模拟单指 2.36ms 与角度 0.55ms，验证 P2 不靠补发旧节拍获得改善。
TEST_CASE("comm: L6 lookahead avoids skipped periods in a feasible deterministic trace", "[comm][l6][interstage]") {
    const auto simulate = [](bool lookahead) {
        InterstageOptions options;
        options.query_lookahead = lookahead;
        options.finger_estimate = 2360us;
        options.timing_margin = 0us;
        options.angle_budget = 1ms;
        Clock::time_point now{};
        auto due = now;
        const auto deadline = now + 33333us;
        uint64_t skipped = 0;
        unsigned queries = 0;
        const auto serve = [&] {
            const auto periods = (now - due) / 2ms + 1;
            skipped += static_cast<uint64_t>(periods - 1);
            due += periods * 2ms;
            now += 550us;
            ++queries;
        };
        serve();
        for (size_t finger = 0; finger < 5; ++finger) {
            unsigned inserted = 0;
            while (true) {
                const auto action = detail::boundary_action(options, now, due, 2ms, deadline, 5 - finger, inserted);
                if (action == detail::BoundaryAction::WaitForAngle) {
                    now = due;
                } else if (action == detail::BoundaryAction::QueryAngle) {
                    serve();
                    ++inserted;
                } else {
                    break;
                }
            }
            now += 2360us;
        }
        if (now >= due) {
            serve();
        }
        REQUIRE(now < deadline);
        REQUIRE(queries > 1);
        return skipped;
    };
    REQUIRE(simulate(false) > 0);
    REQUIRE(simulate(true) == 0);
}

// 真实 SDK 路径应在完整手指之间查询角度；关闭插入时保留旧的五指连读。
TEST_CASE("comm: L6 scheduler interleaves only at complete finger boundaries", "[comm][l6][interstage]") {
    for (const unsigned limit : {0u, 1u}) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        transport->on_request(0x28, [](const Message &request) {
            if (request.dlc == 2) {
                std::this_thread::sleep_for(3ms);
            }
            return response(request);
        });
        L6 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
        L6PollingPlan plan;
        plan.angle_enabled = plan.tactile_enabled = true;
        plan.angle_period = 1ms;
        plan.tactile_period = 200ms;
        plan.interstage.max_angle_transactions = limit;
        plan.interstage.angle_budget = 20ms;
        hand.start_polling(plan);
        // Completion can be published before the next scheduler tick records
        // skipped angle periods (especially when interstage insertion is off).
        // Wait for that observation before stopping the worker we are testing.
        const bool complete = await([&] {
            return hand.get_snapshot().force_sensor.has_value() &&
                   hand.get_runtime_stats().angle_skipped_deadline_count > 0;
        });
        hand.stop_polling();
        REQUIRE(complete);
        const auto sent = raw->sent();
        bool in_batch = false;
        unsigned between = 0;
        unsigned since_finger = 0;
        for (const auto &message : sent) {
            if (message.data[0] == 0xB1) {
                in_batch = true;
            }
            if (in_batch && message.data[0] == 0x01) {
                ++between;
                ++since_finger;
            }
            if (in_batch && message.dlc == 2) {
                REQUIRE(since_finger <= limit);
                since_finger = 0;
            }
            if (message.data[0] == 0xB5) {
                break;
            }
        }
        REQUIRE((between > 0) == (limit > 0));
        const auto stats = hand.get_runtime_stats();
        REQUIRE(stats.tactile_completed_count == 1);
        REQUIRE(stats.finger_latency[0].count == 1);
        REQUIRE(stats.finger_latency[4].count == 1);
        REQUIRE(stats.pending_count == 0);
        REQUIRE(stats.in_flight_count == 0);
        REQUIRE(stats.angle_skipped_deadline_count > 0);
    }
}

// 统计窗口只接纳窗口内的新响应，不把末端排空或不同来源回包重复计数。
TEST_CASE("comm: L6 benchmark excludes responses outside its receive window", "[comm][l6][interstage]") {
    l6_benchmark::WindowSamples samples;
    samples.begin = Clock::time_point{} + 1s;
    samples.end = samples.begin + 1s;
    samples.angle(samples.begin - 1us, SampleOrigin::Query);
    samples.angle(samples.begin, SampleOrigin::Query);
    samples.angle(samples.begin + 2ms, SampleOrigin::CommandFeedback);
    samples.angle(samples.end, SampleOrigin::Query);
    samples.force(samples.end - 1us);
    samples.force(samples.end);
    l6_benchmark::Metrics metrics;
    samples.apply(metrics);
    REQUIRE(metrics.angle_query_count == 1);
    REQUIRE(metrics.angle_command_feedback_count == 1);
    REQUIRE(metrics.outside_angle_window_count == 2);
    REQUIRE(metrics.tactile_completed_count == 1);
    REQUIRE(metrics.outside_tactile_window_count == 1);
}

// 原始层的嵌套查询不能清空外层触觉在途状态，五指完成后必须排空。
TEST_CASE("comm: raw L6 stages keep truthful pending state and complete samples", "[comm][l6][interstage]") {
    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(0x28, response);
    l6_benchmark::RawSession session(std::move(transport), 0x28, 1ms);
    unsigned boundaries = 0;
    (void)session.query_all_tactile(500ms, [&](size_t index, auto) {
        REQUIRE(session.metrics().in_flight_count == 1);
        REQUIRE(session.metrics().pending_count == 0);
        if (index != 0) {
            ++boundaries;
            (void)session.query_angle(50ms);
            REQUIRE(session.metrics().in_flight_count == 1);
        }
    });
    const auto metrics = session.metrics();
    REQUIRE(boundaries == 4);
    REQUIRE(metrics.angle_query_count == 4);
    REQUIRE(metrics.tactile_completed_count == 1);
    REQUIRE(metrics.finger_latency[4].count == 1);
    REQUIRE(metrics.pending_count == 0);
    REQUIRE(metrics.in_flight_count == 0);
}

// 指间嵌套事务失败时禁止发送下一指或发布不完整触觉，并清理在途状态。
TEST_CASE("comm: raw L6 aborts tactile batch after failed nested angle query", "[comm][l6][interstage]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    transport->on_request(0x28, [](const Message &request) {
        return request.data[0] == 0x01 ? std::vector<Message>{} : response(request);
    });
    l6_benchmark::RawSession session(std::move(transport), 0x28, 1ms);
    REQUIRE_THROWS(session.query_all_tactile(200ms, [&](size_t index, auto) {
        if (index == 1) {
            (void)session.query_angle(5ms);
        }
    }));
    const auto sent = raw->sent();
    REQUIRE(sent.size() == 2);
    REQUIRE(sent[0].data[0] == 0xB1);
    REQUIRE(sent[1].data[0] == 0x01);
    const auto metrics = session.metrics();
    REQUIRE(metrics.tactile_completed_count == 0);
    REQUIRE(metrics.timeout_count == 1);
    REQUIRE(metrics.angle_timeout_count == 1);
    REQUIRE(metrics.tactile_timeout_count == 0);
    REQUIRE(metrics.pending_count == 0);
    REQUIRE(metrics.in_flight_count == 0);
}

// 停止原始层时正在等待的单指事务及时退出，后续手指不得发送。
TEST_CASE("comm: raw L6 stop cancels a pending finger", "[comm][l6][interstage]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    l6_benchmark::RawSession session(std::move(transport), 0x28, 1ms);
    std::atomic<bool> stop{false};
    session.set_stop_predicate([&] { return stop.load(); });
    auto task = std::async(std::launch::async, [&] {
        try {
            (void)session.query_all_tactile(2s);
            return false;
        } catch (const std::exception &) {
            return true;
        }
    });
    const bool pending = await([&] { return session.metrics().pending_count == 1; });
    stop.store(true);
    REQUIRE(pending);
    REQUIRE(task.wait_for(200ms) == std::future_status::ready);
    REQUIRE(task.get());
    REQUIRE(raw->sent().size() == 1);
    REQUIRE(session.metrics().in_flight_count == 0);
}

// SDK 到达绝对截止时间后不再启动新轮询，窗口外的事务不会继续累计。
TEST_CASE("comm: L6 polling cutoff prevents new transactions", "[comm][l6][interstage]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    transport->on_request(0x28, response);
    L6 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.angle_enabled = true;
    plan.angle_period = 2ms;
    plan.polling_until = Clock::now() + 30ms;
    hand.start_polling(plan);
    std::this_thread::sleep_until(*plan.polling_until + 20ms);
    const auto count = raw->sent().size();
    std::this_thread::sleep_for(20ms);
    hand.stop_polling();
    REQUIRE(count > 0);
    REQUIRE(raw->sent().size() == count);
}

// 强制停止会取消已发角度事务，随后到达的一帧应稳定记为迟到响应。
TEST_CASE("comm: L6 forced polling stop reproduces one late angle frame", "[comm][l6][interstage]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::atomic<bool> requested{false};
    transport->on_request(0x28, [&](const Message &) {
        requested.store(true);
        return std::vector<Message>{};
    });
    L6 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.angle_enabled = true;
    plan.angle_period = 100ms;
    hand.start_polling(plan);
    REQUIRE(await([&] { return requested.load(); }));
    hand.stop_polling();

    Message request;
    request.id = 0x28;
    request.dlc = 1;
    request.data[0] = 0x01;
    raw->inject(response(request).front());
    REQUIRE(await([&] { return hand.get_runtime_stats().late_response_count == 1; }));
}

// 强制停止会取消已发单指事务，其十二行回包应全部记为迟到响应。
TEST_CASE("comm: L6 forced polling stop reproduces twelve late tactile frames", "[comm][l6][interstage]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::atomic<bool> requested{false};
    transport->on_request(0x28, [&](const Message &request) {
        if (request.data[0] == 0xB1) {
            requested.store(true);
        }
        return std::vector<Message>{};
    });
    L6 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.tactile_enabled = true;
    plan.tactile_period = 100ms;
    hand.start_polling(plan);
    REQUIRE(await([&] { return requested.load(); }));
    hand.stop_polling();

    Message request;
    request.id = 0x28;
    request.dlc = 2;
    request.data[0] = 0xB1;
    request.data[1] = 0xC6;
    for (const auto &frame : response(request)) {
        raw->inject(frame);
    }
    REQUIRE(await([&] { return hand.get_runtime_stats().late_response_count == 12; }));
}

// graceful stop 等待当前线上事务完成，角度和触觉都不制造停止边界迟到帧。
TEST_CASE("comm: L6 graceful polling stop drains the current wire transaction", "[comm][l6][interstage]") {
    for (const bool tactile : {false, true}) {
        auto transport = std::make_unique<FakeTransport>();
        std::atomic<bool> responding{false};
        transport->on_request(0x28, [&](const Message &request) {
            responding.store(true);
            std::this_thread::sleep_for(20ms);
            return response(request);
        });
        L6 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
        L6PollingPlan plan;
        plan.angle_enabled = !tactile;
        plan.tactile_enabled = tactile;
        plan.angle_period = 100ms;
        plan.tactile_period = 100ms;
        hand.start_polling(plan);
        REQUIRE(await([&] { return responding.load(); }));
        REQUIRE(hand.stop_polling_gracefully(200ms));
        REQUIRE(hand.get_runtime_stats().late_response_count == 0);
        REQUIRE(hand.get_runtime_stats().pending_count == 0);
        REQUIRE(hand.get_runtime_stats().in_flight_count == 0);
        REQUIRE_FALSE(hand.runtime_health().polling_faulted);
    }
}

// graceful stop 超过期限后必须强制取消，不能被无响应设备无限阻塞。
TEST_CASE("comm: L6 graceful polling stop has a bounded forced fallback", "[comm][l6][interstage]") {
    auto transport = std::make_unique<FakeTransport>();
    std::atomic<bool> requested{false};
    transport->on_request(0x28, [&](const Message &) {
        requested.store(true);
        return std::vector<Message>{};
    });
    L6 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.angle_enabled = true;
    plan.angle_period = 100ms;
    plan.request_timeout = 2s;
    hand.start_polling(plan);
    REQUIRE(await([&] { return requested.load(); }));

    const auto started = Clock::now();
    REQUIRE_FALSE(hand.stop_polling_gracefully(20ms));
    REQUIRE(Clock::now() - started < 200ms);
    REQUIRE(hand.get_runtime_stats().pending_count == 0);
    REQUIRE(hand.get_runtime_stats().in_flight_count == 0);
}

// 单指缺最后一帧时，已到期角度也不能越过安全边界；补齐后才允许插入。
TEST_CASE("comm: L6 does not interleave within an incomplete finger", "[comm][l6][interstage]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::atomic<bool> waiting{false};
    transport->on_request(0x28, [&](const Message &request) {
        auto frames = response(request);
        if (request.data[0] == 0xB1) {
            frames.pop_back();
            waiting.store(true);
        }
        return frames;
    });
    L6 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.angle_enabled = plan.tactile_enabled = true;
    plan.angle_period = 1ms;
    plan.tactile_period = 500ms;
    plan.interstage.max_angle_transactions = 1;
    plan.interstage.angle_budget = 20ms;
    hand.start_polling(plan);
    const bool entered = await([&] { return waiting.load(); });
    std::this_thread::sleep_for(5ms);
    const auto before = raw->sent();
    Message request;
    request.id = 0x28;
    request.dlc = 2;
    request.data[0] = 0xB1;
    request.data[1] = 0xC6;
    raw->inject(response(request).back());
    const bool complete = await([&] { return hand.get_snapshot().force_sensor.has_value(); });
    hand.stop_polling();
    REQUIRE(entered);
    REQUIRE(before.size() >= 2);
    // The initial boundary may serve another due angle before finger 1.
    // Nothing may be sent after the incomplete finger request.
    for (size_t i = 0; i + 1 < before.size(); ++i) {
        REQUIRE(before[i].data[0] == 0x01);
    }
    REQUIRE(before.back().data[0] == 0xB1);
    REQUIRE(complete);
    REQUIRE(hand.get_runtime_stats().interstage_angle_count > 0);
}

// 显式的短硬超时稳定复现嵌套角度失败；迟到帧必须归因到插空角度，
// 且当前触觉批次不能继续第二指或发布残缺快照。
TEST_CASE("comm: L6 attributes a late nested angle timeout and aborts tactile", "[comm][l6][interstage]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    std::atomic<bool> first_finger{false};
    std::atomic<bool> nested_angle_requested{false};
    Message nested_angle_request;
    transport->on_request(0x28, [&](const Message &request) {
        if (request.data[0] == 0xB1) {
            first_finger.store(true);
            std::this_thread::sleep_for(3ms);
        }
        if (first_finger.load() && request.data[0] == 0x01) {
            nested_angle_request = request;
            nested_angle_requested.store(true);
            return std::vector<Message>{};
        }
        return response(request);
    });
    L6 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.angle_enabled = plan.tactile_enabled = true;
    plan.angle_period = 1ms;
    plan.tactile_period = 500ms;
    plan.interstage.max_angle_transactions = 1;
    plan.interstage.angle_budget = 2ms;
    plan.interstage.request_timeout = 2ms;
    hand.start_polling(plan);
    REQUIRE(await([&] { return nested_angle_requested.load(); }));
    REQUIRE(await([&] { return hand.get_runtime_stats().interstage_angle_timeout_count == 1; }));
    raw->inject(response(nested_angle_request).front());
    REQUIRE(await([&] { return hand.get_runtime_stats().late_response_count == 1; }));
    hand.stop_polling();
    const auto stats = hand.get_runtime_stats();
    REQUIRE(stats.angle_timeout_count == 1);
    REQUIRE(stats.interstage_angle_timeout_count == 1);
    REQUIRE(stats.tactile_timeout_count == 0);
    for (const auto &message : raw->sent()) {
        REQUIRE(message.data[0] != 0xB2);
    }
    REQUIRE_FALSE(hand.get_snapshot().force_sensor.has_value());
    REQUIRE(stats.in_flight_count == 0);
    REQUIRE(stats.pending_count == 0);
}

// 角度预算只用于准入估算。超过 2ms 预算但落在独立硬超时内的响应应成功，
// 证明微秒预算不再被向下取整成事务超时。
TEST_CASE("comm: L6 interstage request timeout is independent from angle budget", "[comm][l6][interstage]") {
    auto transport = std::make_unique<FakeTransport>();
    std::atomic<bool> first_finger{false};
    transport->on_request(0x28, [&](const Message &request) {
        if (request.data[0] == 0xB1) {
            first_finger.store(true);
            std::this_thread::sleep_for(3ms);
        } else if (first_finger.load() && request.data[0] == 0x01) {
            std::this_thread::sleep_for(3ms);
        }
        return response(request);
    });
    L6 hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    L6PollingPlan plan;
    plan.angle_enabled = plan.tactile_enabled = true;
    plan.angle_period = 1ms;
    plan.tactile_period = 500ms;
    plan.interstage.max_angle_transactions = 1;
    plan.interstage.angle_budget = 2ms;
    plan.interstage.request_timeout = 20ms;
    hand.start_polling(plan);
    REQUIRE(await([&] { return hand.get_snapshot().force_sensor.has_value(); }));
    hand.stop_polling();
    const auto stats = hand.get_runtime_stats();
    REQUIRE(stats.interstage_angle_count > 0);
    REQUIRE(stats.angle_timeout_count == 0);
    REQUIRE(stats.interstage_angle_timeout_count == 0);
    REQUIRE(stats.tactile_timeout_count == 0);
}

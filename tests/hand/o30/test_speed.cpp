#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "hand/o30/o30_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::o30;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
    constexpr auto kSampleTimeout = std::chrono::milliseconds{100};
    // 采样间隔：不加节流会把总线打满，反过来拖慢被测的运动本身。
    constexpr auto kSampleInterval = std::chrono::milliseconds{20};
    constexpr auto kSettleTime = std::chrono::seconds{1};
    // 单段 100%→30% 行程的耗时上限。低速档留足余量；跑不到位算失败，不静默通过。
    constexpr auto kTravelBudget = std::chrono::seconds{20};

    // 人眼对比用中低/中高档；1 和 255 差太大，1 计数几乎看不出在动。
    constexpr uint8_t kSlow = 20;
    constexpr uint8_t kFast = 200;
    constexpr uint8_t kProbe = 123;
    // 加速度不拉满时它是瓶颈，两个速度档跑出来几乎一样快，对比就失去意义。
    constexpr uint8_t kMaxAcceleration = 255;

    // 实际角度进到目标 ±10% 视为到位；真机不会精确停在目标百分比上。
    constexpr float kArrivalTolerance = 10.0F;
    // 高速档至少要比低速档快 30%，否则不能算「速度设定生效」。
    constexpr float kMinSpeedup = 1.3F;

    // 交互失败或退出后，Catch2 仍会调度其余用例，这里拦住不再驱动硬件运动。
    bool motion_stopped = false;

    void check_motion_session() {
        if (motion_stopped) {
            // Catch2 按控制台宽度折行会切断多字节字符，消息保持短。
            SKIP("上一步失败或退出，速度动作已停止");
        }
    }

    std::array<uint8_t, kJointCount> filled(uint8_t value) {
        std::array<uint8_t, kJointCount> values{};
        values.fill(value);
        return values;
    }

    struct RestoreSpeeds {
        O30 &hand;
        std::array<uint8_t, kJointCount> original;

        explicit RestoreSpeeds(O30 &h)
            : hand(h), original(h.speed.get_blocking(kTimeout, ValueKind::kSetpoint).speeds) {}

        ~RestoreSpeeds() {
            try {
                hand.speed.set_speeds(original, kTimeout);
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
        }

        RestoreSpeeds(const RestoreSpeeds &) = delete;
        RestoreSpeeds &operator=(const RestoreSpeeds &) = delete;
        RestoreSpeeds(RestoreSpeeds &&) = delete;
        RestoreSpeeds &operator=(RestoreSpeeds &&) = delete;
    };

    // 任何一步抛出（含 REQUIRE 失败）都停掉本轮后续动作，
    // 避免带着未恢复的姿态或故障继续驱动硬件。
    template <typename Action> void guarded_motion(O30 &hand, Action action) {
        try {
            test_util::with_restored_o30_motion(hand, action);
        } catch (...) {
            motion_stopped = true;
            throw;
        }
    }

    struct Travel {
        std::chrono::milliseconds elapsed{0};
        bool arrived = false;
        // 运动窗口内每个关节的当前转速峰值（kActual），不是命令回读。
        std::array<uint8_t, kJointCount> peak{};
    };

    // 只看屈伸轴：开合姿态只改屈伸，旋转/横滚/侧摆保持参考值。
    bool flexion_arrived(const std::array<float, kJointCount> &actual, const Angle &target) {
        for (size_t i = kRoot1JointBegin; i < kJointCount; ++i) {
            if (std::abs(actual[i] - target.values[i]) > kArrivalTolerance) {
                return false;
            }
        }
        return true;
    }

    uint8_t max_peak(const Travel &travel) { return *std::max_element(travel.peak.begin(), travel.peak.end()); }

    // 失败时要看出是哪些关节在转、峰值多少；只报一个 max 没法定位。
    std::string describe_peaks(const Travel &travel) {
        std::string out;
        for (size_t i = 0; i < kJointCount; ++i) {
            if (travel.peak[i] == 0) {
                continue;
            }
            out += std::string(joint_name(i)) + "=" + std::to_string(unsigned(travel.peak[i])) + " ";
        }
        return out.empty() ? std::string("全部为 0") : out;
    }

    // 下发目标后同时轮询实际角度和当前转速，直到屈伸轴全部到位或超出预算。
    // 两路采样的开销对每次调用相同，所以耗时之间仍可横向比较。
    Travel run_stroke(O30 &hand, const Angle &target, std::chrono::milliseconds budget) {
        test_util::o30_require_no_faults(hand);
        Travel out;
        const auto start = std::chrono::steady_clock::now();
        hand.angle.set_angles_percent(target, kTimeout);
        while (true) {
            const auto speeds = hand.speed.get_blocking(kSampleTimeout).speeds;
            for (size_t i = 0; i < kJointCount; ++i) {
                out.peak[i] = std::max(out.peak[i], speeds[i]);
            }
            const auto actual = hand.angle.get_blocking(kSampleTimeout).angles.values;
            out.elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            if (flexion_arrived(actual, target)) {
                out.arrived = true;
                break;
            }
            if (out.elapsed >= budget) {
                break;
            }
            std::this_thread::sleep_for(kSampleInterval);
        }
        return out;
    }

    // 先用高速回起点（这一段不计时），再切到待测速度跑同一段行程并计时。
    Travel timed_stroke(O30 &hand, const Angle &from, const Angle &to, uint8_t speed) {
        hand.speed.set_all(kFast, kTimeout);
        static_cast<void>(run_stroke(hand, from, kTravelBudget));
        std::this_thread::sleep_for(kSettleTime);
        hand.speed.set_all(speed, kTimeout);
        return run_stroke(hand, to, kTravelBudget);
    }
} // namespace

// 真机速度。MI 0x02，20 路 0–255。读 kActual 是当前转速（静止可为 0）；
// 读 kSetpoint 才是刚下发的目标。设定是否真的带动手指，靠下面的计时用例度量，
// 不只靠人眼。

// --- 自动：读写与设定回读 ---

// 静止读 kActual 应有 20 路；0 表示没在转，不是失败。
TEST_CASE("Speed: get_blocking returns 20 actual values", "[o30][control][speed]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.speed.get_blocking(kTimeout);
    REQUIRE(data.kind == ValueKind::kActual);
    REQUIRE(data.speeds.size() == kJointCount);
}

// RTS 设定值也能读到 20 路，和当前转速不是同一条路径。
TEST_CASE("Speed: get_blocking setpoint returns 20 values", "[o30][control][speed]") {
    auto &hand = test_util::o30_hand();
    const auto data = hand.speed.get_blocking(kTimeout, ValueKind::kSetpoint);
    REQUIRE(data.kind == ValueKind::kSetpoint);
    REQUIRE(data.speeds.size() == kJointCount);
}

// set_all 写统一速度，kSetpoint 读回应是刚下发的值。
TEST_CASE("Speed: set_all echoes on setpoint readback", "[o30][control][speed][configuration]") {
    auto &hand = test_util::o30_hand();
    const RestoreSpeeds restore(hand);

    REQUIRE_NOTHROW(hand.speed.set_all(kProbe, kTimeout));
    const auto data = hand.speed.get_blocking(kTimeout, ValueKind::kSetpoint);
    REQUIRE(data.kind == ValueKind::kSetpoint);
    REQUIRE(data.speeds == filled(kProbe));
}

// set_speeds 各关节不同，确认不是只能写同一值。
TEST_CASE("Speed: set_speeds per-joint echoes on setpoint readback", "[o30][control][speed][configuration]") {
    auto &hand = test_util::o30_hand();
    const RestoreSpeeds restore(hand);

    std::array<uint8_t, kJointCount> target{};
    for (size_t i = 0; i < kJointCount; ++i) {
        target[i] = static_cast<uint8_t>(20U + i * 10U);
    }
    REQUIRE_NOTHROW(hand.speed.set_speeds(target, kTimeout));
    REQUIRE(hand.speed.get_blocking(kTimeout, ValueKind::kSetpoint).speeds == target);
}

// timeout<=0（含负值）该立刻抛，读写都不许落到总线上。
TEST_CASE("Speed: rejects non-positive timeout", "[o30][control][speed]") {
    auto &hand = test_util::o30_hand();
    REQUIRE_THROWS_AS(hand.speed.get_blocking(std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.get_blocking(std::chrono::milliseconds{-5}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.set_all(kProbe, std::chrono::milliseconds{0}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(hand.speed.set_speeds(filled(kProbe), std::chrono::milliseconds{-5}),
                      linkerbot::hand::ValidationError);
}

// get_blocking 之后，管理器和整机快照里 speed 都应有值。
TEST_CASE("Speed: snapshot populated after read", "[o30][control][speed]") {
    auto &hand = test_util::o30_hand();
    static_cast<void>(hand.speed.get_blocking(kTimeout));
    REQUIRE(hand.speed.get_snapshot().has_value());
    REQUIRE(hand.get_snapshot().speed.has_value());
}

// --- 自动：运动中度量 ---

// 走一次开合，运动窗口内至少一个关节当前转速 > 0；失败时报出各关节峰值。
TEST_CASE("Speed: actual counts rise while joints move", "[o30][control][speed][motion]") {
    check_motion_session();
    auto &hand = test_util::o30_hand();
    guarded_motion(hand, [&](const Angle &reference) {
        hand.speed.set_all(kFast, kTimeout);
        const auto opening = run_stroke(hand, test_util::o30_open_pose(reference), kTravelBudget);
        const auto closing = run_stroke(hand, test_util::o30_closed_pose(reference), kTravelBudget);
        INFO("张开：耗时 " << opening.elapsed.count() << " ms，到位=" << opening.arrived << "，峰值 "
                           << describe_peaks(opening));
        INFO("握拢：耗时 " << closing.elapsed.count() << " ms，到位=" << closing.arrived << "，峰值 "
                           << describe_peaks(closing));
        REQUIRE(std::max(max_peak(opening), max_peak(closing)) > 0);
    });
}

// 同一段行程分别按 20 和 200 跑，高速档耗时至少短 30%。
// 这是速度设定生效的自动证据，不依赖人眼判断快慢。
TEST_CASE("Speed: higher setting shortens travel time", "[o30][control][speed][motion]") {
    check_motion_session();
    auto &hand = test_util::o30_hand();
    guarded_motion(hand, [&](const Angle &reference) {
        hand.acceleration.set_all(kMaxAcceleration, kTimeout);
        const auto open = test_util::o30_open_pose(reference);
        const auto closed = test_util::o30_closed_pose(reference);

        const auto slow = timed_stroke(hand, open, closed, kSlow);
        const auto fast = timed_stroke(hand, open, closed, kFast);

        INFO("低速 " << unsigned(kSlow) << "：耗时 " << slow.elapsed.count() << " ms，到位=" << slow.arrived
                     << "，峰值 " << describe_peaks(slow));
        INFO("高速 " << unsigned(kFast) << "：耗时 " << fast.elapsed.count() << " ms，到位=" << fast.arrived
                     << "，峰值 " << describe_peaks(fast));
        // 任一档没跑到位就说明这次测量无效，不能拿超时耗时去比快慢。
        REQUIRE(slow.arrived);
        REQUIRE(fast.arrived);
        REQUIRE(static_cast<float>(slow.elapsed.count()) >= kMinSpeedup * static_cast<float>(fast.elapsed.count()));
    });
}

// --- 交互：人眼确认 ---

// 人眼确认低速开合慢、高速开合快，同时打印实测耗时供对照；
// 逐步引导（按回车才动作），失败即停；异常或退出都恢复原速度、加速度和姿态。
TEST_CASE("Speed: interactive speed affects movement", "[o30][control][speed][interactive][motion]") {
    check_motion_session();
    auto &hand = test_util::o30_hand();
    const test_util::O30PollingGuard guard(hand);
    guarded_motion(hand, [&](const Angle &reference) {
        hand.acceleration.set_all(kMaxAcceleration, kTimeout);
        const auto open = test_util::o30_open_pose(reference);
        const auto closed = test_util::o30_closed_pose(reference);

        std::cout << "\n每步先看说明，按回车才动作，再输入 y/n 确认。\n"
                     "n=失败，s=未完成，q=退出；均停止本轮后续速度动作。\n"
                     "每步会先用高速回到起点，再按待测速度跑，只有后一段计时。\n"
                     "结束或退出时都会恢复原速度、加速度和姿态。\n";

        test_util::InteractiveSession session("o30_speed_affects_movement");
        session
            .step(
                "即将以低速 " + std::to_string(unsigned(kSlow)) + " 张开五指",
                [&] {
                    const auto travel = timed_stroke(hand, closed, open, kSlow);
                    std::cout << "反馈：低速张开耗时 " << travel.elapsed.count()
                              << " ms，到位=" << (travel.arrived ? "是" : "否") << "，转速峰值 "
                              << describe_peaks(travel) << "\n";
                },
                "手指应明显缓慢张开；比下一步慢得出来才选 y")
            .step(
                "即将以高速 " + std::to_string(unsigned(kFast)) + " 握拢五指",
                [&] {
                    const auto travel = timed_stroke(hand, open, closed, kFast);
                    std::cout << "反馈：高速握拢耗时 " << travel.elapsed.count()
                              << " ms，到位=" << (travel.arrived ? "是" : "否") << "，转速峰值 "
                              << describe_peaks(travel) << "\n";
                },
                "手指应明显更快握拢；和上一步耗时对比应明显更短");

        try {
            session.run(true);
        } catch (...) {
            session.save_report();
            throw;
        }
        session.save_report();
        REQUIRE_FALSE(session.quit_early());
        REQUIRE(session.failed_steps().empty());
    });
}

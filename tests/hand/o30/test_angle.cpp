#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "hand/o30/comm_helpers.hpp"
#include "hand/o30/o30_fixture.hpp"
#include "hand/support/interactive_session.hpp"

using namespace linkerbot::hand::o30;

namespace {
    constexpr auto kTimeout = std::chrono::milliseconds{500};
    constexpr auto kSettleTime = std::chrono::seconds{2};
    constexpr float kTolerance = 15.0F;
    constexpr float kStationaryTolerance = 5.0F;
    constexpr float kMinimumTravel = 10.0F;
    // q、失败或跳过后，Catch2 即使继续调度其他用例，也不再执行角度动作。
    bool motion_stopped = false;
    constexpr std::array<const char *, kJointCount> kChineseNames{
        "拇指旋转",         "拇指横滚",         "食指侧摆",         "中指侧摆",           "无名指侧摆",
        "小指侧摆",         "拇指根部第一关节", "食指根部第一关节", "中指根部第一关节",   "无名指根部第一关节",
        "小指根部第一关节", "食指根部第二关节", "中指根部第二关节", "无名指根部第二关节", "小指根部第二关节",
        "拇指指尖关节",     "食指指尖关节",     "中指指尖关节",     "无名指指尖关节",     "小指指尖关节"};

    struct FingerFlex {
        const char *name;
        std::vector<size_t> joints;
    };

    const std::array<FingerFlex, 5> kFingerFlex{{
        {"拇指", {6, 15}},
        {"食指", {7, 11, 16}},
        {"中指", {8, 12, 17}},
        {"无名指", {9, 13, 18}},
        {"小指", {10, 14, 19}},
    }};

    void require_no_faults(O30 &hand) {
        const auto faults = hand.fault.get_blocking(kTimeout);
        const bool modern = faults.revision == protocol::Revision::kV004;
        std::ostringstream problems;
        for (size_t i = 0; i < kJointCount; ++i) {
            if (faults.faults[i] == 0) {
                continue;
            }
            problems << "\n  " << kChineseNames[i] << "（SI 0x" << std::hex << std::uppercase << std::setw(2)
                     << std::setfill('0') << unsigned(joint_slot(i)) << "）：原始故障 0x" << std::setw(2)
                     << unsigned(faults.faults[i]) << std::dec;
            if (modern && faults.has_fault(i, FaultCode::kActuatorLayerStall)) {
                problems << "，包含执行器层堵转标志（原因需现场确认）";
            }
        }
        if (!problems.str().empty()) {
            throw std::runtime_error(
                std::string("设备报告故障，停止本轮角度动作。HOP ") +
                (modern ? "0.0.4，故障对象 MI 0x0C：" : "0.0.3，故障对象 MI 0x0D：") + problems.str() +
                "\n请检查对应关节是否受阻，并按设备说明处理故障后重新测试；SDK 未提供已验证的清故障命令。");
        }
    }

    void check_motion_session() {
        if (motion_stopped) {
            SKIP("本轮已取消或未通过；后续角度动作已停止。重新运行命令可重新测试。");
        }
    }

    // 仅在实际下发过动作后恢复。恢复同样打印提示并检查故障及设定值。
    template <typename Action> void with_restored_pose(O30 &hand, Action action) {
        std::cout << "\n[动作前检查] 正在读取协议版本、故障状态和原姿态；此阶段不会发送动作。\n" << std::flush;
        // 前置读取也属于本轮测试：故障或通信异常只失败一次，后续动作用例跳过。
        const auto original = [&] {
            try {
                require_no_faults(hand);
                return project_u8(hand.angle.get_slots(kTimeout, ValueKind::kSetpoint));
            } catch (...) {
                motion_stopped = true;
                std::cout << "[动作前检查未通过] 未发送动作，后续角度动作测试将跳过。\n" << std::flush;
                throw;
            }
        }();
        std::cout << "[动作前检查通过] 接下来按中文提示逐步确认。\n";
        bool moved = false;
        const auto restore = [&] {
            if (!moved) {
                return;
            }
            std::cout << "\n正在恢复本组测试前的姿态，请观察手指；恢复完成后才结束本组。\n" << std::flush;
            require_no_faults(hand);
            hand.angle.set_angles(original, kTimeout);
            std::this_thread::sleep_for(kSettleTime);
            require_no_faults(hand);
            REQUIRE(project_u8(hand.angle.get_slots(kTimeout, ValueKind::kSetpoint)) == original);
            std::cout << "原姿态设定值已恢复。\n";
        };
        try {
            action(original, moved);
        } catch (...) {
            motion_stopped = true;
            try {
                restore();
            } catch (...) {
                WARN("原姿态恢复失败或设备存在故障，请检查手的当前状态。");
            }
            throw;
        }
        try {
            restore();
        } catch (...) {
            motion_stopped = true;
            throw;
        }
    }

    void run_session(test_util::InteractiveSession &session) {
        std::cout << "\n每步先看中文说明，按回车才动作，再输入 y/n 确认。\n"
                     "n=失败，s=未完成，q=退出；均停止本轮后续角度动作。\n"
                     "已执行动作的测试结束或退出时会尝试恢复原姿态；设备故障时不再写入。\n";
        try {
            session.run(true);
        } catch (...) {
            motion_stopped = true;
            session.save_report();
            throw;
        }
        session.save_report();
        motion_stopped = session.quit_early() || !session.failed_steps().empty();
        REQUIRE_FALSE(session.quit_early());
        REQUIRE(session.failed_steps().empty());
    }

    void write_pose(O30 &hand, const std::array<uint8_t, kJointCount> &raw) {
        require_no_faults(hand);
        hand.angle.set_angles(raw, kTimeout);
        std::this_thread::sleep_for(kSettleTime);
        require_no_faults(hand);
        REQUIRE(project_u8(hand.angle.get_slots(kTimeout, ValueKind::kSetpoint)) == raw);
    }

    float mean_percent(const std::array<float, kJointCount> &values, const std::vector<size_t> &joints) {
        float sum = 0.0F;
        for (const auto joint : joints) {
            sum += values[joint];
        }
        return sum / static_cast<float>(joints.size());
    }

    void print_finger_flexion(const Angle &pose, const std::array<float, kJointCount> &actual) {
        for (const auto &finger : kFingerFlex) {
            std::cout << "  " << finger.name << "：目标 " << mean_percent(pose.values, finger.joints) << "%，实际 "
                      << mean_percent(actual, finger.joints) << "%\n";
        }
    }

    std::string endpoint_description(size_t joint) {
        if (joint == 0) {
            return "向食指靠近";
        }
        if (joint == 1) {
            return "向垂直掌心的方向横滚";
        }
        return "向远离拇指的方向侧摆";
    }

    Angle apply_finger_flexions(const Angle &reference, const std::array<float, 5> &flexions) {
        auto values = reference.values;
        for (size_t finger = 0; finger < kFingerFlex.size(); ++finger) {
            for (const auto joint : kFingerFlex[finger].joints) {
                values[joint] = flexions[finger];
            }
        }
        return Angle::from_array(values);
    }

    void check_flexion_poses(const std::string &report, const std::vector<float> &targets) {
        check_motion_session();
        auto &hand = test_util::o30_hand();
        with_restored_pose(hand, [&](const auto &original, bool &moved) {
            test_util::InteractiveSession session(report);
            for (const float target : targets) {
                const auto label =
                    target == 100 ? "五指伸直（100%）" : "五指弯曲，目标 " + std::to_string(int(target)) + "%";
                session.step(
                    label,
                    [&, target] {
                        const auto pose = test_util::o30_flexion_pose(Angle::from_sensor_raw(original), target);
                        moved = true;
                        write_pose(hand, pose.to_raw());
                        const auto actual = hand.angle.get_blocking(kTimeout).angles.values;
                        print_finger_flexion(pose, actual);
                        for (size_t i = 0; i < kJointCount; ++i) {
                            INFO("关节=" << kChineseNames[i]);
                            REQUIRE(std::abs(actual[i] - pose.values[i]) <= kTolerance);
                        }
                    },
                    target == 100
                        ? "五指屈伸关节全部伸直；拇指旋转、横滚和四指侧摆保持原位置。"
                        : "五指屈伸关节弯曲到指定程度；拇指旋转、横滚和四指侧摆保持原位置。百分比不是角度单位。");
            }
            run_session(session);
        });
    }

    struct CheckFingerFlexionPoseArguments {
        const std::string &report;
        const std::string &label;
        const std::string &expected;
        const std::array<float, 5> &flexions;
    };
    void check_finger_flexion_pose(const CheckFingerFlexionPoseArguments &arguments) {
        check_motion_session();
        auto &hand = test_util::o30_hand();
        with_restored_pose(hand, [&](const auto &original, bool &moved) {
            test_util::InteractiveSession session(arguments.report);
            session.step(
                arguments.label,
                [&] {
                    const auto pose = apply_finger_flexions(Angle::from_sensor_raw(original), arguments.flexions);
                    moved = true;
                    write_pose(hand, pose.to_raw());
                    const auto actual = hand.angle.get_blocking(kTimeout).angles.values;
                    print_finger_flexion(pose, actual);
                    for (size_t i = 0; i < kJointCount; ++i) {
                        INFO("关节=" << kChineseNames[i]);
                        REQUIRE(std::abs(actual[i] - pose.values[i]) <= kTolerance);
                    }
                },
                arguments.expected);
            run_session(session);
        });
    }
} // namespace

TEST_CASE("comm: O30 preflight fault stops all subsequent angle actions", "[comm][o30][angle][interaction]") {
    struct RestoreState {
        bool original = motion_stopped;
        ~RestoreState() { motion_stopped = original; }
    } restore_state;
    motion_stopped = false;
    auto h = o30_comm::make_hand();
    h.hop->put_ascii(0x41, 0x61, 8, "0.0.4");
    auto faults = std::vector<uint8_t>(36, 0);
    faults[7] = 2; // 实机复现：中指侧摆 SI07 = 02。
    h.hop->put(0x0C, faults);
    size_t attempted = 0;
    size_t moved = 0;
    std::string diagnostic;
    for (size_t test = 0; test < 7; ++test) {
        if (motion_stopped) {
            continue;
        }
        ++attempted;
        try {
            with_restored_pose(*h.hand, [&](const auto &, bool &) { ++moved; });
        } catch (const std::runtime_error &error) {
            diagnostic = error.what();
        }
    }
    REQUIRE(attempted == 1);
    REQUIRE(moved == 0);
    REQUIRE(motion_stopped);
    REQUIRE(diagnostic.find("中指侧摆（SI 0x07）：原始故障 0x02") != std::string::npos);
    REQUIRE(diagnostic.find("HOP 0.0.4，故障对象 MI 0x0C") != std::string::npos);
    REQUIRE(h.hop->writes.empty());
    REQUIRE(h.transport->sent().size() == 2); // 只查版本和故障，不再读/写姿态。
}

TEST_CASE("O30 角度：读取 20 个关节的百分比", "[o30][control][angle]") {
    const auto data = test_util::o30_hand().angle.get_blocking(kTimeout);
    REQUIRE(data.angles.values.size() == kJointCount);
    for (size_t i = 0; i < kJointCount; ++i) {
        INFO("关节=" << kChineseNames[i]);
        REQUIRE(data.angles.values[i] >= 0.0F);
        REQUIRE(data.angles.values[i] <= 100.0F);
    }
}

TEST_CASE("O30 角度交互：五指开合", "[o30][control][angle][interactive][motion][open_close]") {
    check_flexion_poses("o30_angle_open_close", {100.0F, 30.0F, 100.0F});
}

// 握拳后按拇→食→中→无名→小各张开一次，不再逐关节、也不再跑两套指令。
TEST_CASE("O30 角度交互：五指逐个张开", "[o30][control][angle][flexion][motion][interactive]") {
    check_motion_session();
    auto &hand = test_util::o30_hand();
    with_restored_pose(hand, [&](const auto &original, bool &moved) {
        auto pose = original;
        for (size_t i = kRoot1JointBegin; i < kJointCount; ++i) {
            pose[i] = percent_to_raw(30.0F);
        }

        test_util::InteractiveSession session("o30_angle_individual_fingers");
        session.step(
            "五指先握拳，百分比目标 30%",
            [&] {
                moved = true;
                write_pose(hand, pose);
                const auto actual = hand.angle.get_blocking(kTimeout).angles.values;
                print_finger_flexion(Angle::from_sensor_raw(pose), actual);
                for (size_t i = kRoot1JointBegin; i < kJointCount; ++i) {
                    REQUIRE(std::abs(actual[i] - 30.0F) <= kTolerance);
                }
            },
            "五指弯曲；拇指旋转、横滚和四指侧摆保持原位置。");

        for (const auto &finger : kFingerFlex) {
            session.step(
                std::string(finger.name) + "张开",
                [&, finger] {
                    for (const auto joint : finger.joints) {
                        pose[joint] = percent_to_raw(100.0F);
                    }
                    write_pose(hand, pose);
                    const auto actual = hand.angle.get_blocking(kTimeout).angles.values;
                    const auto target = Angle::from_sensor_raw(pose);
                    std::cout << "反馈：" << finger.name << " 实际 " << mean_percent(actual, finger.joints) << "%\n";
                    print_finger_flexion(target, actual);
                    REQUIRE(std::abs(mean_percent(actual, finger.joints) - 100.0F) <= kTolerance);
                    REQUIRE(mean_percent(actual, finger.joints) - 30.0F >= kMinimumTravel);
                    for (size_t i = 0; i < kJointCount; ++i) {
                        REQUIRE(std::abs(actual[i] - target.values[i]) <= kTolerance);
                    }
                },
                std::string("只有") + finger.name +
                    "伸直；已经张开的手指保持伸直，还没轮到的手指保持弯曲。错指或方向反了选 n。");
        }
        run_session(session);
    });
}

// 拇指伸直，食指到小指握到 70%；旋转、横滚和侧摆保持原位置。
TEST_CASE("O30 角度交互：四指紧握，大拇指张开", "[o30][control][angle][interactive][motion][open_close][flexion]") {
    check_finger_flexion_pose({"o30_angle_four_fingers_close_thumb_open",
                               "四指握拳（0%），拇指伸直（100%）",
                               "食指、中指、无名指、小指弯曲握紧；大拇指伸直。旋转、横滚和侧摆保持原位置。",
                               {100.0F, 0.0F, 0.0F, 0.0F, 0.0F}});
}

// 拇指旋转、横滚和四指侧摆各看一次方向；准备位在同一步里完成，不再往返确认。
TEST_CASE("O30 角度交互：拇指姿态与四指侧摆", "[o30][control][angle][yaw][roll][rotation][motion][interactive]") {
    check_motion_session();
    auto &hand = test_util::o30_hand();
    with_restored_pose(hand, [&](const auto &original, bool &moved) {
        const auto original_pct = Angle::from_sensor_raw(original).values;
        test_util::InteractiveSession session("o30_angle_thumb_and_yaw");
        for (size_t joint = 0; joint < 6; ++joint) {
            const std::string name = kChineseNames[joint];
            session.step(
                name + "一次",
                [&, joint, name] {
                    moved = true;
                    auto raw = original;
                    raw[joint] = joint_percent_to_raw(joint, 30.0F);
                    write_pose(hand, raw);
                    const auto at_prep = hand.angle.get_blocking(kTimeout).angles.values[joint];
                    raw[joint] = joint_percent_to_raw(joint, 70.0F);
                    write_pose(hand, raw);
                    const auto actual = hand.angle.get_blocking(kTimeout).angles.values;
                    std::cout << "反馈：" << name << " 实际 " << actual[joint] << "%\n";
                    REQUIRE(std::abs(actual[joint] - 70.0F) <= kTolerance);
                    REQUIRE(actual[joint] - at_prep >= kMinimumTravel);
                    for (size_t i = 0; i < kJointCount; ++i) {
                        if (i == joint) {
                            continue;
                        }
                        REQUIRE(std::abs(actual[i] - original_pct[i]) <= kStationaryTolerance);
                    }
                },
                "仅" + name + endpoint_description(joint) + "；其他关节保持不动。方向反了或错指选 n。");
        }
        run_session(session);
    });
}

TEST_CASE("O30 角度：读取后快照有效", "[o30][control][angle]") {
    auto &hand = test_util::o30_hand();
    static_cast<void>(hand.angle.get_blocking(kTimeout));
    REQUIRE(hand.get_snapshot().angle.has_value());
}

TEST_CASE("O30 角度：读取 20 个半字位置", "[o30][control][angle]") {
    REQUIRE(test_util::o30_hand().angle.get_halfword(kTimeout).positions.size() == kJointCount);
}

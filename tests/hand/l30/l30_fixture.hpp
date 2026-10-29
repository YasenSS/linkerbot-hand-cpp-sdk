#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>

#include <linkerbot/hand/l30/l30.hpp>

namespace test_util {

    using namespace linkerbot::hand::l30;

    inline uint8_t get_l30_node_id() {
        const char *env = std::getenv("L30_NODE_ID");
        if (env != nullptr) {
            const auto value = std::atoi(env);
            if (value >= protocol::kNodeIdMin && value <= protocol::kNodeIdMax) {
                return static_cast<uint8_t>(value);
            }
        }
        return kDefaultNodeId;
    }

    inline std::string get_l30_interface() {
        const char *env = std::getenv("CAN_INTERFACE");
        if (env != nullptr) {
            return {env};
        }
        return "can0";
    }

    inline std::string get_l30_tester_name() {
        const char *env = std::getenv("TESTER");
        if (env != nullptr) {
            return {env};
        }
        return "anonymous";
    }

    // Shared across every [l30] hardware case: opening the FD interface twice
    // in one process would fight over the same socket.
    inline L30 &l30_hand() {
        static L30 hand(get_l30_interface(), get_l30_node_id());
        return hand;
    }

    // from_sensor_raw 不夹取。真机过冲会略微超出命令行程 [0, 100]；
    // 行程 1600 的根部屈曲关节实机读回过 -20.8125 %，所以放宽到 ±30 %。
    inline constexpr float kAngleReadLowerBound = -30.0F;
    inline constexpr float kAngleReadUpperBound = 130.0F;

    inline std::array<float, kJointCount> all_joints_l30(float value) {
        std::array<float, kJointCount> values{};
        values.fill(value);
        return values;
    }

    // Percent 50 puts the four abduction joints and the wrist at raw 0 (their
    // specs are symmetric), which is the safe neutral pose to start from.
    inline Angle l30_neutral_pose() { return Angle::from_array(all_joints_l30(50.0F)); }

    inline Angle l30_open_pose() {
        auto values = all_joints_l30(50.0F);
        // Keep the original raw-zero opening target under the reversed mapping.
        for (const size_t index : {0U, 1U, 5U, 6U, 7U, 8U, 9U, 10U, 14U, 15U}) {
            values.at(index) = 100.0F;
        }
        return Angle::from_array(values);
    }

    inline Angle l30_closed_pose() {
        auto values = all_joints_l30(50.0F);
        for (const size_t index : {0U, 1U, 5U, 6U, 7U, 8U, 9U, 10U, 14U, 15U}) {
            values.at(index) = 40.0F;
        }
        return Angle::from_array(values);
    }

    inline void move_and_wait_l30(L30 &hand, const Angle &angles, int wait_sec = 2) {
        hand.angle.set_angles_percent(angles);
        std::this_thread::sleep_for(std::chrono::seconds{wait_sec});
    }

    struct L30PollingGuard {
        L30 &hand;

        explicit L30PollingGuard(L30 &h) : hand(h) {}

        ~L30PollingGuard() {
            try {
                hand.stop_stream();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            try {
                hand.stop_polling();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
        }

        L30PollingGuard(const L30PollingGuard &) = delete;
        L30PollingGuard &operator=(const L30PollingGuard &) = delete;
        L30PollingGuard(L30PollingGuard &&) = delete;
        L30PollingGuard &operator=(L30PollingGuard &&) = delete;
    };

} // namespace test_util

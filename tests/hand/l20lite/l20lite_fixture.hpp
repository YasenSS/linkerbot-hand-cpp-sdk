#pragma once

#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include <linkerbot/hand/l20lite/l20lite.hpp>

namespace test_util {

    using namespace linkerbot::hand::l20lite;

    inline Side get_l20lite_side() {
        const char *env = std::getenv("L20LITE_SIDE");
        if (env != nullptr) {
            std::string val(env);
            if (val == "right") {
                return Side::kRight;
            }
        }
        return Side::kLeft;
    }

    inline std::string get_interface() {
        const char *env = std::getenv("CAN_INTERFACE");
        if (env != nullptr) {
            return {env};
        }
        return "can0";
    }

    inline L20lite &l20lite_hand() {
        static L20lite hand(get_l20lite_side(), get_interface());
        return hand;
    }

    inline std::array<float, Angle::kJointCount> all_joints(float value) {
        std::array<float, Angle::kJointCount> values{};
        values.fill(value);
        return values;
    }

    // Hardware quirks: thumb_abd often stays near 100; closed pose keeps it there.
    inline Angle open_pose() { return Angle(100, 50, 100, 100, 100, 100, 50, 50, 50, 50); }

    inline Angle closed_pose() { return Angle(28, 100, 0, 0, 0, 0, 100, 100, 100, 100); }

    inline void move_and_wait(L20lite &hand, const Angle &angles, int wait_sec = 2) {
        hand.angle.set_angles_percent(angles);
        std::this_thread::sleep_for(std::chrono::seconds{wait_sec});
    }

    struct L20litePollingGuard {
        L20lite &hand;

        explicit L20litePollingGuard(L20lite &h) : hand(h) {}

        ~L20litePollingGuard() {
            try {
                hand.stop_stream();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            try {
                hand.stop_polling();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
        }

        L20litePollingGuard(const L20litePollingGuard &) = delete;
        L20litePollingGuard &operator=(const L20litePollingGuard &) = delete;
        L20litePollingGuard(L20litePollingGuard &&) = delete;
        L20litePollingGuard &operator=(L20litePollingGuard &&) = delete;
    };

} // namespace test_util

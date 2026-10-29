#pragma once

#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include <linkerbot/hand/l20/l20.hpp>

namespace test_util {

    using namespace linkerbot::hand::l20;

    inline Side get_l20_side() {
        const char *env = std::getenv("L20_SIDE");
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

    inline L20 &l20_hand() {
        static L20 hand(get_l20_side(), get_interface());
        return hand;
    }

    inline std::array<float, Angle::kJointCount> all_joints(float value) {
        std::array<float, Angle::kJointCount> values{};
        values.fill(value);
        return values;
    }

    // Hardware-verified open pose from the Python L20 tests (natural splay).
    inline Angle open_pose() {
        return Angle(100, 100, 100, 100, 100, 100, 100, 67, 100, 100, 33, 100, 100, 0, 100, 100);
    }

    // Hardware-verified closed pose from the Python L20 tests.
    inline Angle closed_pose() { return Angle(100, 50, 55, 55, 50, 0, 0, 50, 0, 0, 50, 0, 0, 50, 0, 0); }

    inline void move_and_wait(L20 &hand, const Angle &angles, int wait_sec = 2) {
        hand.angle.set_angles_percent(angles);
        std::this_thread::sleep_for(std::chrono::seconds{wait_sec});
    }

    struct L20PollingGuard {
        L20 &hand;

        explicit L20PollingGuard(L20 &h) : hand(h) {}

        ~L20PollingGuard() {
            try {
                hand.stop_stream();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            try {
                hand.stop_polling();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
        }

        L20PollingGuard(const L20PollingGuard &) = delete;
        L20PollingGuard &operator=(const L20PollingGuard &) = delete;
        L20PollingGuard(L20PollingGuard &&) = delete;
        L20PollingGuard &operator=(L20PollingGuard &&) = delete;
    };

} // namespace test_util

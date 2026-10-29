#pragma once

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include <linkerbot/hand/l6/l6.hpp>

namespace test_util {

    using namespace linkerbot::hand::l6;

    inline Side get_side() {
        const char *env = std::getenv("L6_SIDE");
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

    inline std::string get_tester_name() {
        const char *env = std::getenv("TESTER");
        if (env != nullptr) {
            return {env};
        }
        return "anonymous";
    }

    inline L6 &l6_hand() {
        static L6 hand(get_side(), get_interface());
        return hand;
    }

    inline void move_and_wait(L6 &hand, const Angle &angles, int wait_sec = 2) {
        hand.angle.set_angles_percent(angles);
        std::this_thread::sleep_for(std::chrono::seconds{wait_sec});
    }

    struct PollingGuard {
        L6 &hand;

        explicit PollingGuard(L6 &h) : hand(h) {}

        ~PollingGuard() {
            try {
                hand.stop_stream();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            try {
                hand.stop_polling();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
        }

        PollingGuard(const PollingGuard &) = delete;
        PollingGuard &operator=(const PollingGuard &) = delete;
        PollingGuard(PollingGuard &&) = delete;
        PollingGuard &operator=(PollingGuard &&) = delete;
    };

} // namespace test_util

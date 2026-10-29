#pragma once

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include <linkerbot/hand/o6/o6.hpp>

namespace test_util {

    using namespace linkerbot::hand::o6;

    inline Side get_o6_side() {
        const char *env = std::getenv("O6_SIDE");
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

    inline O6 &o6_hand() {
        static O6 hand(get_o6_side(), get_interface());
        return hand;
    }

    inline void move_and_wait(O6 &hand, const Angle &angles, int wait_sec = 2) {
        hand.angle.set_angles_percent(angles);
        std::this_thread::sleep_for(std::chrono::seconds{wait_sec});
    }

    struct O6PollingGuard {
        O6 &hand;

        explicit O6PollingGuard(O6 &h) : hand(h) {}

        ~O6PollingGuard() {
            try {
                hand.stop_stream();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            try {
                hand.stop_polling();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
        }

        O6PollingGuard(const O6PollingGuard &) = delete;
        O6PollingGuard &operator=(const O6PollingGuard &) = delete;
        O6PollingGuard(O6PollingGuard &&) = delete;
        O6PollingGuard &operator=(O6PollingGuard &&) = delete;
    };

} // namespace test_util

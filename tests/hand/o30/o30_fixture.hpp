#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace test_util {

    using namespace linkerbot::hand::o30;

    inline std::string get_o30_interface() {
        const char *env = std::getenv("CAN_INTERFACE");
        if (env != nullptr) {
            return {env};
        }
        return "can0";
    }

    inline uint32_t get_o30_request_id() {
        const char *env = std::getenv("O30_REQUEST_ID");
        if (env != nullptr) {
            const auto value = std::strtoul(env, nullptr, 0);
            if (value <= 0x3FFUL) {
                return static_cast<uint32_t>(value);
            }
        }
        return protocol::kDefaultRequestId;
    }

    inline std::string get_o30_tester_name() {
        const char *env = std::getenv("TESTER");
        if (env != nullptr) {
            return {env};
        }
        return "anonymous";
    }

    // Shared across every [o30] hardware case: opening the FD interface twice
    // in one process would fight over the same socket.
    inline O30 &o30_hand() {
        static O30 hand(get_o30_interface(), get_o30_request_id());
        return hand;
    }

    inline std::array<float, kJointCount> all_joints_o30(float value) {
        std::array<float, kJointCount> values{};
        values.fill(value);
        return values;
    }

    // Change flexion only; retain thumb rotation, thumb roll and abduction.
    inline Angle o30_flexion_pose(const Angle &reference, float flexion) {
        auto values = reference.values;
        for (size_t i = kRoot1JointBegin; i < kJointCount; ++i) {
            values[i] = flexion;
        }
        return Angle::from_array(values);
    }

    // The default uses midpoint yaw/roll; it is not a calibrated neutral pose.
    inline Angle o30_open_pose(const Angle &reference = Angle::from_array(all_joints_o30(50.0F))) {
        return o30_flexion_pose(reference, 100.0F);
    }

    inline Angle o30_closed_pose(const Angle &reference = Angle::from_array(all_joints_o30(50.0F))) {
        return o30_flexion_pose(reference, 30.0F);
    }

    inline Angle o30_neutral_pose(const Angle &reference = Angle::from_array(all_joints_o30(50.0F))) {
        return o30_flexion_pose(reference, 70.0F);
    }

    inline void move_and_wait_o30(O30 &hand, const Angle &angles, int wait_sec = 2) {
        hand.angle.set_angles_percent(angles);
        std::this_thread::sleep_for(std::chrono::seconds{wait_sec});
    }

    // 故障闸门要能定位：只报关节名无法判断是真故障还是 0.0.4 的保留位，
    // 所以带上原始字节、已定义位、保留位和读到的协议版本。
    inline std::string o30_fault_detail(const FaultData &faults, size_t joint) {
        const auto status = faults.status(joint);
        std::ostringstream out;
        out << joint_name(joint) << "[" << joint << "]=0x" << std::hex << std::uppercase << std::setw(2)
            << std::setfill('0') << unsigned(status.raw);
        if (status.known_bits != 0) {
            out << "(已定义位 0x" << std::setw(2) << unsigned(status.known_bits) << ")";
        }
        if (status.reserved_bits != 0) {
            out << "(保留位 0x" << std::setw(2) << unsigned(status.reserved_bits) << ")";
        }
        return out.str();
    }

    // 一次报出全部故障关节，而不是在第一个就抛——否则看不出故障范围。
    inline void o30_require_no_faults(O30 &hand) {
        const auto faults = hand.fault.get_blocking(std::chrono::milliseconds{500});
        std::string blocked;
        for (size_t i = 0; i < kJointCount; ++i) {
            if (faults.status(i).has_any_bit()) {
                blocked += (blocked.empty() ? "" : "，") + o30_fault_detail(faults, i);
            }
        }
        if (!blocked.empty()) {
            throw std::runtime_error(std::string("O30i motion blocked by fault (HOP ") +
                                     (faults.revision == protocol::Revision::kV004 ? "0.0.4" : "0.0.3") +
                                     "): " + blocked);
        }
    }

    // Hardware speed experiments change pose and acceleration too. Restore
    // all three on success or failure, and surface restoration failure.
    template <typename Action> void with_restored_o30_motion(O30 &hand, Action action) {
        const auto timeout = std::chrono::milliseconds{500};
        o30_require_no_faults(hand);
        const auto pose = project_u8(hand.angle.get_slots(timeout, ValueKind::kSetpoint));
        const auto speed = hand.speed.get_blocking(timeout, ValueKind::kSetpoint).speeds;
        const auto accel = hand.acceleration.get_blocking(timeout, ValueKind::kSetpoint).accelerations;
        const auto restore = [&] {
            o30_require_no_faults(hand);
            hand.speed.set_speeds(speed, timeout);
            hand.acceleration.set_accelerations(accel, timeout);
            hand.angle.set_angles(pose, timeout);
            if (project_u8(hand.angle.get_slots(timeout, ValueKind::kSetpoint)) != pose ||
                hand.speed.get_blocking(timeout, ValueKind::kSetpoint).speeds != speed ||
                hand.acceleration.get_blocking(timeout, ValueKind::kSetpoint).accelerations != accel) {
                throw std::runtime_error("O30i motion restoration readback mismatch");
            }
        };
        try {
            action(Angle::from_sensor_raw(pose));
        } catch (...) {
            try {
                restore();
            } catch (const std::exception &e) {
                std::cerr << "O30i restoration failed: " << e.what() << '\n';
            }
            throw;
        }
        restore();
    }

    struct O30PollingGuard {
        O30 &hand;

        explicit O30PollingGuard(O30 &h) : hand(h) {}

        ~O30PollingGuard() {
            try {
                hand.stop_stream();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
            try {
                hand.stop_polling();
            } catch (...) { // NOLINT(bugprone-empty-catch)
            }
        }

        O30PollingGuard(const O30PollingGuard &) = delete;
        O30PollingGuard &operator=(const O30PollingGuard &) = delete;
        O30PollingGuard(O30PollingGuard &&) = delete;
        O30PollingGuard &operator=(O30PollingGuard &&) = delete;
    };

} // namespace test_util

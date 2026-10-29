#include "traced_transport.hpp"
#include <cmath>
#include <thread>

void check(bool ok, const char *message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        return 2;
    }
    std::cout << std::unitbuf;
    try {
        O30 hand(std::make_unique<TracedTransport>(argv[1], argv[2]));
        const auto identity = hand.version.get_verified_product_info(2000ms, linkerbot::hand::IdentitySide::kRight);
        check(identity.protocol_version == "0.0.4", "unexpected protocol");
        check(identity.app_version == "1.1.0", "wrong application version");
        check(identity.build_time == "2026-09-14 10:53:01", "wrong build date");
        std::cout << "PASS identity app=" << identity.app_version << " side=" << identity.hand_side << '\n';
        const auto faults = hand.fault.get_blocking(500ms);
        for (size_t i = 0; i < kJointCount; ++i) {
            check(!faults.has_any_fault(i), "hardware reports a fault");
        }
        bool read_rejected = false;
        try {
            static_cast<void>(hand.enable.get_blocking(500ms));
        } catch (const std::logic_error &) {
            read_rejected = true;
        }
        check(read_rejected, "unsupported enable read was accepted");
        bool rejected = false;
        try {
            hand.enable.set_masks({}, 500ms);
        } catch (const std::logic_error &) {
            rejected = true;
        }
        check(rejected, "unsupported enable write was accepted");
        std::cout << "PASS faults; unsupported enable read/write rejected\n";
        const auto errors = hand.comm_error.get_history(500ms);
        check(errors.history.size() == 15, "unexpected history length");
        std::cout << "PASS error history actual_length=" << errors.history.size() << '\n';

        const auto original_sensor = hand.sensor.get_info(500ms).selected;
        bool selected = false;
        try {
            for (uint8_t sensor = 1; sensor <= 5; ++sensor) {
                selected = true;
                hand.sensor.select(sensor, 500ms);
                const auto data = hand.sensor.read_data(500ms);
                check(data.info.selected == sensor, "wrong sensor selected");
                check(data.info.total_length == data.payload.size(), "incomplete tactile data");
                check(data.info.descriptor == "FnS_U16_1;Fn_U8_70", "unexpected tactile descriptor");
                check(data.info.rows == 10 && data.info.columns == 7, "wrong tactile dimensions");
                std::cout << "PASS sensor=" << +sensor << " descriptor=" << data.info.descriptor
                          << " length=" << data.payload.size() << " rows=" << +data.info.rows
                          << " columns=" << +data.info.columns << '\n';
            }
            hand.sensor.select(original_sensor, 500ms);
            check(hand.sensor.get_info(500ms).selected == original_sensor, "sensor restore failed");
            selected = false;
            std::cout << "RESTORE sensor=" << +original_sensor << " verified\n";
        } catch (...) {
            if (selected) {
                hand.sensor.select(original_sensor, 500ms);
            }
            throw;
        }

        const auto original_target = project_u8(hand.angle.get_slots(500ms, ValueKind::kSetpoint));
        const auto before = project_u8(hand.angle.get_slots(500ms));
        auto target = original_target;
        target[16] = original_target[16] <= 243 ? original_target[16] + 12 : original_target[16] - 12;
        bool moved = false;
        auto restore = [&] {
            hand.angle.set_angles(original_target, 500ms);
            std::this_thread::sleep_for(1500ms);
            check(project_u8(hand.angle.get_slots(500ms, ValueKind::kSetpoint)) == original_target,
                  "full pose restore failed");
            std::cout << "RESTORE full pose targets verified; actual=";
            values(project_u8(hand.angle.get_slots(500ms)));
            std::cout << '\n';
        };
        try {
            moved = true;
            hand.angle.set_sparse_joints({{16, target[16]}}, 500ms);
            std::this_thread::sleep_for(1500ms);
            const auto setpoint = project_u8(hand.angle.get_slots(500ms, ValueKind::kSetpoint));
            const auto actual = project_u8(hand.angle.get_slots(500ms));
            std::cout << "SPARSE before=";
            values(before);
            std::cout << " target=";
            values(target);
            std::cout << " setpoint=";
            values(setpoint);
            std::cout << " actual=";
            values(actual);
            std::cout << '\n';
            check(setpoint == target, "sparse changed non-target setpoints or missed target");
            check(std::abs(int(actual[16]) - int(target[16])) <= 5, "index tip did not reach target");
            check(std::abs(int(actual[16]) - int(before[16])) >= 3, "index tip did not move");
            for (size_t i = 0; i < kJointCount; ++i) {
                if (i != 16) {
                    check(std::abs(int(actual[i]) - int(before[i])) <= 5,
                          "non-target actual position changed significantly");
                }
            }
            std::cout << "PASS sparse API index tip reached; all other targets unchanged\n";
            restore();
            moved = false;
        } catch (...) {
            if (moved) {
                restore();
            }
            throw;
        }
        std::cout << "PASS all five fixes verified\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}

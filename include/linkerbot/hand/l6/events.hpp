#pragma once

#include <chrono>
#include <optional>
#include <variant>

#include <linkerbot/hand/l6/angle.hpp>
#include <linkerbot/hand/l6/current.hpp>
#include <linkerbot/hand/l6/current_limit.hpp>
#include <linkerbot/hand/l6/fault.hpp>
#include <linkerbot/hand/l6/force_sensor.hpp>
#include <linkerbot/hand/l6/raw_control_manager.hpp>
#include <linkerbot/hand/l6/stall.hpp>
#include <linkerbot/hand/l6/temperature.hpp>
#include <linkerbot/hand/l6/torque.hpp>

namespace linkerbot::hand::l6 {

    struct AngleEvent {
        AngleData data;
    };
    struct TorqueEvent {
        TorqueData data;
    };
    struct CurrentLimitEvent {
        CurrentLimitData data;
    };
    struct TemperatureEvent {
        TemperatureData data;
    };
    struct CurrentEvent {
        CurrentData data;
    };
    struct FaultEvent {
        FaultData data;
    };
    struct ForceSensorEvent {
        AllFingersData data;
    };

    using SensorEvent = std::variant<AngleEvent, CurrentLimitEvent, TorqueEvent, TemperatureEvent, CurrentEvent,
                                     FaultEvent, ForceSensorEvent>;

    enum class SensorSource {
        kAngle,
        kCurrentLimit,
        kTorque,
        kTemperature,
        kCurrent,
        kFault,
        kForceSensor,
    };

    struct Snapshot {
        std::optional<AngleData> angle;
        std::optional<CurrentLimitData> current_limit;
        std::optional<TorqueData> torque;
        std::optional<TemperatureData> temperature;
        std::optional<CurrentData> current;
        std::optional<FaultData> fault;
        std::optional<AllFingersData> force_sensor;
        std::optional<StallTimeData> stall_time;
        std::optional<StallLimitData> stall_threshold;
        std::optional<StallLimitData> stall_hold_current;
        std::chrono::steady_clock::time_point timestamp;
        std::optional<RawControlData> position_kp;
        std::optional<RawControlData> position_kd;
        std::optional<RawControlData> torque_kd;
        std::optional<RawControlData> limit_compensations;
        std::optional<RawControlData> configured_speeds;
    };

} // namespace linkerbot::hand::l6

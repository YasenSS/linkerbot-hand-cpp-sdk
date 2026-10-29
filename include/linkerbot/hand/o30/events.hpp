#pragma once

#include <chrono>
#include <optional>
#include <string_view>
#include <variant>

#include <linkerbot/hand/o30/angle.hpp>
#include <linkerbot/hand/o30/fault.hpp>
#include <linkerbot/hand/o30/motion.hpp>
#include <linkerbot/hand/o30/sensor.hpp>
#include <linkerbot/hand/o30/stall.hpp>

namespace linkerbot::hand::o30 {

    struct AngleEvent {
        AngleData data;
    };
    struct SpeedEvent {
        SpeedData data;
    };
    struct AccelerationEvent {
        AccelerationData data;
    };
    struct CurrentEvent {
        CurrentData data;
    };
    struct CurrentLimitEvent {
        CurrentLimitData data;
    };
    struct VoltageEvent {
        VoltageData data;
    };
    struct TorqueEvent {
        TorqueData data;
    };
    struct TorqueLimitEvent {
        TorqueLimitData data;
    };
    struct TemperatureEvent {
        TemperatureData data;
    };
    struct MotionTimeEvent {
        MotionTimeData data;
    };
    struct StallTimeEvent {
        StallTimeData data;
    };
    struct FaultEvent {
        FaultData data;
    };
    struct ForceSensorEvent {
        SensorData data;
    };

    using SensorEvent = std::variant<AngleEvent, SpeedEvent, AccelerationEvent, CurrentEvent, CurrentLimitEvent,
                                     VoltageEvent, TorqueEvent, TorqueLimitEvent, TemperatureEvent, MotionTimeEvent,
                                     StallTimeEvent, FaultEvent, ForceSensorEvent>;

    enum class SensorSource {
        kAngle,
        kSpeed,
        kAcceleration,
        kCurrent,
        kCurrentLimit,
        kVoltage,
        kTorque,
        kTorqueLimit,
        kTemperature,
        kMotionTime,
        kStallTime,
        kFault,
        kForceSensor,
        kAllForceSensors
    };

    [[nodiscard]] std::string_view sensor_source_name(SensorSource source);

    struct Snapshot {
        std::optional<AngleData> angle;
        std::optional<SpeedData> speed;
        std::optional<AccelerationData> acceleration;
        std::optional<CurrentData> current;
        std::optional<CurrentLimitData> current_limit;
        std::optional<VoltageData> voltage;
        std::optional<TorqueData> torque;
        std::optional<TorqueLimitData> torque_limit;
        std::optional<TemperatureData> temperature;
        std::optional<MotionTimeData> motion_time;
        std::optional<StallTimeData> stall_time;
        std::optional<StallThresholdData> stall_threshold;
        std::optional<StallHoldCurrentData> stall_hold_current;
        std::optional<FaultData> fault;
        std::optional<SensorData> force_sensor;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o30

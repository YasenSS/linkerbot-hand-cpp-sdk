#pragma once

#include <chrono>
#include <optional>
#include <variant>

#include <linkerbot/hand/o6/acceleration.hpp>
#include <linkerbot/hand/o6/angle.hpp>
#include <linkerbot/hand/o6/current.hpp>
#include <linkerbot/hand/o6/fault.hpp>
#include <linkerbot/hand/o6/force_sensor.hpp>
#include <linkerbot/hand/o6/speed.hpp>
#include <linkerbot/hand/o6/stall.hpp>
#include <linkerbot/hand/o6/temperature.hpp>

namespace linkerbot::hand::o6 {

    struct AngleEvent {
        AngleData data;
    };
    struct CurrentEvent {
        CurrentData data;
    };
    struct CurrentLimitEvent {
        CurrentLimitData data;
    };
    struct SpeedEvent {
        SpeedData data;
    };
    struct AccelerationEvent {
        AccelerationData data;
    };
    struct TemperatureEvent {
        TemperatureData data;
    };
    struct FaultEvent {
        FaultData data;
    };
    struct ForceSensorEvent {
        AllFingersData data;
    };

    using SensorEvent = std::variant<AngleEvent, CurrentEvent, CurrentLimitEvent, SpeedEvent, AccelerationEvent,
                                     TemperatureEvent, FaultEvent, ForceSensorEvent>;

    enum class SensorSource {
        kAngle,
        kCurrent,
        kSpeed,
        kAcceleration,
        kTemperature,
        kFault,
        kForceSensor,
    };

    struct Snapshot {
        std::optional<AngleData> angle;
        std::optional<CurrentData> current;
        std::optional<CurrentLimitData> current_limit;
        std::optional<SpeedData> speed;
        std::optional<AccelerationData> acceleration;
        std::optional<TemperatureData> temperature;
        std::optional<FaultData> fault;
        std::optional<AllFingersData> force_sensor;
        std::optional<StallTimeData> stall_time;
        std::optional<StallLimitData> stall_threshold;
        std::optional<StallLimitData> stall_hold_current;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::o6

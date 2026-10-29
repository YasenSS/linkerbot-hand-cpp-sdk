#pragma once

#include <chrono>
#include <optional>
#include <variant>

#include <linkerbot/hand/l20lite/acceleration.hpp>
#include <linkerbot/hand/l20lite/angle.hpp>
#include <linkerbot/hand/l20lite/current.hpp>
#include <linkerbot/hand/l20lite/fault.hpp>
#include <linkerbot/hand/l20lite/force_sensor.hpp>
#include <linkerbot/hand/l20lite/speed.hpp>
#include <linkerbot/hand/l20lite/stall.hpp>
#include <linkerbot/hand/l20lite/temperature.hpp>
#include <linkerbot/hand/l20lite/tip_force.hpp>

namespace linkerbot::hand::l20lite {

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
    struct TipForceEvent {
        TipForceData data;
    };

    using SensorEvent = std::variant<AngleEvent, CurrentEvent, CurrentLimitEvent, SpeedEvent, AccelerationEvent,
                                     TemperatureEvent, FaultEvent, ForceSensorEvent, TipForceEvent>;

    enum class SensorSource {
        kAngle,
        kCurrent,
        kSpeed,
        kAcceleration,
        kTemperature,
        kFault,
        kForceSensor,
        kTipForce,
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
        std::optional<TipForceData> tip_force;
        std::optional<StallTimeData> stall_time;
        std::optional<StallLimitData> stall_threshold;
        std::optional<StallLimitData> stall_hold_current;
        std::chrono::steady_clock::time_point timestamp;
        std::optional<SpeedData> configured_speeds;
    };

} // namespace linkerbot::hand::l20lite

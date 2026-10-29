#pragma once

#include <chrono>
#include <optional>
#include <variant>

#include <linkerbot/hand/l20/angle.hpp>
#include <linkerbot/hand/l20/current.hpp>
#include <linkerbot/hand/l20/fault.hpp>
#include <linkerbot/hand/l20/force_sensor.hpp>
#include <linkerbot/hand/l20/speed.hpp>
#include <linkerbot/hand/l20/temperature.hpp>

namespace linkerbot::hand::l20 {

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
    struct TemperatureEvent {
        TemperatureData data;
    };
    struct FaultEvent {
        FaultData data;
    };
    struct ForceSensorEvent {
        AllFingersData data;
    };

    using SensorEvent = std::variant<AngleEvent, CurrentEvent, CurrentLimitEvent, SpeedEvent, TemperatureEvent,
                                     FaultEvent, ForceSensorEvent>;

    enum class SensorSource {
        kAngle,
        kCurrent,
        kSpeed,
        kTemperature,
        kFault,
        kForceSensor,
    };

    struct Snapshot {
        std::optional<AngleData> angle;
        std::optional<CurrentData> current;
        std::optional<CurrentLimitData> current_limit;
        std::optional<SpeedData> speed;
        std::optional<TemperatureData> temperature;
        std::optional<FaultData> fault;
        std::optional<AllFingersData> force_sensor;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l20

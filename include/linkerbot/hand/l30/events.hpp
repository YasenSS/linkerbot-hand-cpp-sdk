#pragma once

#include <chrono>
#include <optional>
#include <string_view>
#include <variant>

#include <linkerbot/hand/l30/angle.hpp>
#include <linkerbot/hand/l30/fault.hpp>
#include <linkerbot/hand/l30/force_sensor.hpp>
#include <linkerbot/hand/l30/motion.hpp>

namespace linkerbot::hand::l30 {

    struct AngleEvent {
        AngleData data;
    };

    struct SpeedEvent {
        SpeedData data;
    };

    struct CurrentLimitEvent {
        CurrentLimitData data;
    };
    struct CurrentEvent {
        CurrentData data;
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

    using SensorEvent = std::variant<AngleEvent, SpeedEvent, CurrentLimitEvent, CurrentEvent, TemperatureEvent,
                                     FaultEvent, ForceSensorEvent>;

    // Sources that host-side polling can drive. Current limits are command
    // state, so there is nothing to poll for.
    enum class SensorSource { kAngle, kSpeed, kCurrent, kTemperature, kFault, kForceSensor };

    [[nodiscard]] std::string_view sensor_source_name(SensorSource source);

    // Non-blocking aggregate of whatever each manager has cached. An entry is
    // empty until that source has produced data at least once.
    struct Snapshot {
        std::optional<AngleData> angle;
        std::optional<SpeedData> speed;
        std::optional<CurrentLimitData> current_limit;
        std::optional<CurrentData> current;
        std::optional<TemperatureData> temperature;
        std::optional<FaultData> fault;
        std::optional<AllFingersData> force_sensor;
        std::chrono::steady_clock::time_point timestamp;
        std::optional<SpeedData> configured_speeds;
    };

} // namespace linkerbot::hand::l30

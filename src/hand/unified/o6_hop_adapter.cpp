#include "detail/model_adapters.hpp"

#include <algorithm>
#include <array>

namespace linkerbot::hand::unified_detail {
    CommandOutcome submit_command(o6::HopO6 &device, const CommandRequest &request) {
        if (request.raw_values.size() != 6) {
            throw ValidationError("O6 HOP commands require six joint values");
        }
        std::array<uint8_t, 6> raw{};
        std::copy(request.raw_values.begin(), request.raw_values.end(), raw.begin());
        switch (request.semantic) {
        case CommandSemantic::kAngles:
            device.angle.set_angles(raw, request.timeout);
            break;
        case CommandSemantic::kSpeeds:
            device.speed.set_values(raw, request.timeout);
            break;
        case CommandSemantic::kAccelerations:
            device.acceleration.set_values(raw, request.timeout);
            break;
        case CommandSemantic::kCurrentLimits:
            device.current_limit.set_values(raw, request.timeout);
            break;
        case CommandSemantic::kStallDetectionTimes:
            device.stall_detection_time.set_values(raw, request.timeout);
            break;
        case CommandSemantic::kStallThresholds:
            device.stall_threshold.set_values(raw, request.timeout);
            break;
        case CommandSemantic::kStallHoldCurrents:
            device.stall_hold_current.set_values(raw, request.timeout);
            break;
        default:
            throw UnsupportedOperationError("unsupported O6 HOP command");
        }
        return {2, request.ack_expectation != AckExpectation::kNone};
    }

    TelemetrySample read_telemetry(o6::HopO6 &device, TelemetrySemantic semantic, std::chrono::milliseconds timeout) {
        switch (semantic) {
        case TelemetrySemantic::kAngles:
            return device.angle.get_blocking(timeout);
        case TelemetrySemantic::kFaults:
            return device.fault.get_blocking(timeout);
        case TelemetrySemantic::kForce:
            return device.force_sensor.get_blocking(timeout);
        case TelemetrySemantic::kConfiguredSpeeds: {
            ConfiguredSpeedSample sample;
            static_cast<RawVectorSample &>(sample) = device.speed.get_blocking(timeout);
            return sample;
        }
        case TelemetrySemantic::kConfiguredAccelerations: {
            ConfiguredAccelerationSample sample;
            static_cast<RawVectorSample &>(sample) = device.acceleration.get_blocking(timeout);
            return sample;
        }
        case TelemetrySemantic::kCurrents: {
            CurrentSample sample;
            static_cast<RawVectorSample &>(sample) = device.current.get_blocking(timeout);
            return sample;
        }
        case TelemetrySemantic::kCurrentLimits: {
            CurrentLimitSample sample;
            static_cast<RawVectorSample &>(sample) = device.current_limit.get_blocking(timeout);
            return sample;
        }
        case TelemetrySemantic::kTemperatures: {
            TemperatureSample sample;
            static_cast<RawVectorSample &>(sample) = device.temperature.get_blocking(timeout);
            return sample;
        }
        case TelemetrySemantic::kStallDetectionTimes: {
            StallDetectionTimeSample sample;
            static_cast<RawVectorSample &>(sample) = device.stall_detection_time.get_blocking(timeout);
            return sample;
        }
        case TelemetrySemantic::kStallThresholds: {
            StallThresholdSample sample;
            static_cast<RawVectorSample &>(sample) = device.stall_threshold.get_blocking(timeout);
            return sample;
        }
        case TelemetrySemantic::kStallHoldCurrents: {
            StallHoldCurrentSample sample;
            static_cast<RawVectorSample &>(sample) = device.stall_hold_current.get_blocking(timeout);
            return sample;
        }
        default:
            throw UnsupportedOperationError("unsupported O6 HOP telemetry");
        }
    }

    CommandOutcome operate_device(o6::HopO6 &device, DeviceOperation operation, std::chrono::milliseconds timeout) {
        if (operation != DeviceOperation::kSaveParameters) {
            throw UnsupportedOperationError("unsupported O6 HOP operation");
        }
        device.config.save(timeout);
        return {1, true};
    }

    DeviceIdentity read_identity(o6::HopO6 &device, HandSide side, std::chrono::milliseconds timeout) {
        auto result = device.version.get_blocking(timeout);
        if (result.side != side) {
            throw IdentityMismatchError("O6 HOP side does not match configuration");
        }
        return result;
    }

    void start_device_polling(o6::HopO6 &device, const PollingPlan &plan) { device.start_polling(plan); }
} // namespace linkerbot::hand::unified_detail

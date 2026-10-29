#include "detail/model_adapters.hpp"
namespace linkerbot::hand::unified_detail {
    CommandOutcome submit_command(l6::HopL6 &device, const CommandRequest &request) {
        if (request.raw_values.size() != 6) {
            throw ValidationError("L6 HOP commands require six joint values");
        }
        if (request.semantic == CommandSemantic::kAngles) {
            std::array<uint16_t, 6> raw{};
            std::copy(request.raw_values.begin(), request.raw_values.end(), raw.begin());
            device.angle.set_angles(raw, request.timeout);
        } else {
            std::array<uint8_t, 6> raw{};
            std::copy(request.raw_values.begin(), request.raw_values.end(), raw.begin());
            if (request.semantic == CommandSemantic::kSpeeds) {
                device.speed.set_values(raw, request.timeout);
            } else if (request.semantic == CommandSemantic::kCurrentLimits) {
                device.current_limit.set_values(raw, request.timeout);
            } else {
                throw UnsupportedOperationError("unsupported L6 HOP command");
            }
        }
        return {2, request.ack_expectation != AckExpectation::kNone};
    }
    TelemetrySample read_telemetry(l6::HopL6 &device, TelemetrySemantic semantic, std::chrono::milliseconds timeout) {
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
        default:
            throw UnsupportedOperationError("unsupported L6 HOP telemetry");
        }
    }
    CommandOutcome operate_device(l6::HopL6 &device, DeviceOperation operation, std::chrono::milliseconds timeout) {
        if (operation != DeviceOperation::kSaveParameters) {
            throw UnsupportedOperationError("unsupported L6 HOP operation");
        }
        device.config.save(timeout);
        return {1, true};
    }
    DeviceIdentity read_identity(l6::HopL6 &device, HandSide side, std::chrono::milliseconds timeout) {
        auto result = device.version.get_blocking(timeout);
        if (result.side != side) {
            throw IdentityMismatchError("L6 HOP side does not match configuration");
        }
        return result;
    }
    HandSnapshot unified_snapshot(const HandSnapshot &source, Model model, HandSide side, const DeviceCapabilities &) {
        auto result = source;
        result.model = model;
        result.side = side;
        return result;
    }
    void start_device_polling(l6::HopL6 &device, const PollingPlan &plan) { device.start_polling(plan); }
}

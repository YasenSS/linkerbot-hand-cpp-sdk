#pragma once

#include "backend.hpp"

#include <chrono>

#include <linkerbot/hand/api/device.hpp>
#include <linkerbot/hand/l20/l20.hpp>
#include <linkerbot/hand/l20lite/l20lite.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l6/hop_l6.hpp>
#include <linkerbot/hand/l6/l6.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <linkerbot/hand/o6/o6.hpp>

namespace linkerbot::hand::unified_detail {
    struct CommandOutcome {
        uint64_t sent_frames = 0;
        bool acknowledged = false;
    };

    [[nodiscard]] CommandOutcome submit_command(l6::L6 &device, const CommandRequest &request);
    [[nodiscard]] TelemetrySample read_telemetry(l6::L6 &device, TelemetrySemantic semantic,
                                                 std::chrono::milliseconds timeout);
    [[nodiscard]] CommandOutcome operate_device(l6::L6 &device, DeviceOperation operation,
                                                std::chrono::milliseconds timeout);

    [[nodiscard]] CommandOutcome submit_command(l6::HopL6 &device, const CommandRequest &request);
    [[nodiscard]] TelemetrySample read_telemetry(l6::HopL6 &device, TelemetrySemantic semantic,
                                                 std::chrono::milliseconds timeout);
    [[nodiscard]] CommandOutcome operate_device(l6::HopL6 &device, DeviceOperation operation,
                                                std::chrono::milliseconds timeout);

    [[nodiscard]] CommandOutcome submit_command(o6::O6 &device, const CommandRequest &request);
    [[nodiscard]] TelemetrySample read_telemetry(o6::O6 &device, TelemetrySemantic semantic,
                                                 std::chrono::milliseconds timeout);
    [[nodiscard]] CommandOutcome operate_device(o6::O6 &device, DeviceOperation operation,
                                                std::chrono::milliseconds timeout);

    [[nodiscard]] CommandOutcome submit_command(l20lite::L20lite &device, const CommandRequest &request);
    [[nodiscard]] TelemetrySample read_telemetry(l20lite::L20lite &device, TelemetrySemantic semantic,
                                                 std::chrono::milliseconds timeout);
    [[nodiscard]] CommandOutcome operate_device(l20lite::L20lite &device, DeviceOperation operation,
                                                std::chrono::milliseconds timeout);

    [[nodiscard]] CommandOutcome submit_command(l20::L20 &device, const CommandRequest &request);
    [[nodiscard]] TelemetrySample read_telemetry(l20::L20 &device, TelemetrySemantic semantic,
                                                 std::chrono::milliseconds timeout);
    [[nodiscard]] CommandOutcome operate_device(l20::L20 &device, DeviceOperation operation,
                                                std::chrono::milliseconds timeout);

    [[nodiscard]] CommandOutcome submit_command(l30::L30 &device, const CommandRequest &request);
    [[nodiscard]] TelemetrySample read_telemetry(l30::L30 &device, TelemetrySemantic semantic,
                                                 std::chrono::milliseconds timeout);
    [[nodiscard]] CommandOutcome operate_device(l30::L30 &device, DeviceOperation operation,
                                                std::chrono::milliseconds timeout);

    [[nodiscard]] CommandOutcome submit_command(o30::O30 &device, const CommandRequest &request);
    [[nodiscard]] TelemetrySample read_telemetry(o30::O30 &device, TelemetrySemantic semantic,
                                                 std::chrono::milliseconds timeout);
    [[nodiscard]] CommandOutcome operate_device(o30::O30 &device, DeviceOperation operation,
                                                std::chrono::milliseconds timeout);

    [[nodiscard]] AngleSample angle_sample(const l6::AngleData &data, const DeviceCapabilities &capabilities);
    [[nodiscard]] AngleSample angle_sample(const o6::AngleData &data, const DeviceCapabilities &capabilities);
    [[nodiscard]] AngleSample angle_sample(const l20lite::AngleData &data, const DeviceCapabilities &capabilities);
    [[nodiscard]] AngleSample angle_sample(const l20::AngleData &data, const DeviceCapabilities &capabilities);
    [[nodiscard]] AngleSample angle_sample(const l30::AngleData &data, const DeviceCapabilities &capabilities);
    [[nodiscard]] AngleSample angle_sample(const o30::AngleData &data, const DeviceCapabilities &capabilities);

    [[nodiscard]] DeviceIdentity read_identity(l6::L6 &device, HandSide side, std::chrono::milliseconds timeout);
    [[nodiscard]] DeviceIdentity read_identity(o6::O6 &device, HandSide side, std::chrono::milliseconds timeout);
    [[nodiscard]] DeviceIdentity read_identity(l20lite::L20lite &device, HandSide side,
                                               std::chrono::milliseconds timeout);
    [[nodiscard]] DeviceIdentity read_identity(l20::L20 &device, HandSide side, std::chrono::milliseconds timeout);
    [[nodiscard]] DeviceIdentity read_identity(l30::L30 &device, HandSide side, std::chrono::milliseconds timeout);
    [[nodiscard]] DeviceIdentity read_identity(o30::O30 &device, HandSide side, std::chrono::milliseconds timeout);

    [[nodiscard]] HandSnapshot unified_snapshot(const l6::Snapshot &source, Model model, HandSide side,
                                                const DeviceCapabilities &capabilities);
    [[nodiscard]] HandSnapshot unified_snapshot(const o6::Snapshot &source, Model model, HandSide side,
                                                const DeviceCapabilities &capabilities);
    [[nodiscard]] HandSnapshot unified_snapshot(const l20lite::Snapshot &source, Model model, HandSide side,
                                                const DeviceCapabilities &capabilities);
    [[nodiscard]] HandSnapshot unified_snapshot(const l20::Snapshot &source, Model model, HandSide side,
                                                const DeviceCapabilities &capabilities);
    [[nodiscard]] HandSnapshot unified_snapshot(const l30::Snapshot &source, Model model, HandSide side,
                                                const DeviceCapabilities &capabilities);
    [[nodiscard]] HandSnapshot unified_snapshot(const o30::Snapshot &source, Model model, HandSide side,
                                                const DeviceCapabilities &capabilities);

    // Extracts one already-projected channel from a concrete model snapshot.
    // Cache-only telemetry uses this path and fails explicitly when no prior
    // command/response has populated the channel.
    [[nodiscard]] TelemetrySample telemetry_sample(const HandSnapshot &snapshot, TelemetrySemantic semantic);

    [[nodiscard]] DeviceIdentity read_identity(l6::HopL6 &device, HandSide side, std::chrono::milliseconds timeout);
    [[nodiscard]] HandSnapshot unified_snapshot(const HandSnapshot &source, Model model, HandSide side,
                                                const DeviceCapabilities &capabilities);
    void start_device_polling(l6::HopL6 &device, const PollingPlan &plan);
    void start_device_polling(l6::L6 &device, const PollingPlan &plan);
    void start_device_polling(o6::O6 &device, const PollingPlan &plan);
    void start_device_polling(l20lite::L20lite &device, const PollingPlan &plan);
    void start_device_polling(l20::L20 &device, const PollingPlan &plan);
    void start_device_polling(l30::L30 &device, const PollingPlan &plan);
    void start_device_polling(o30::O30 &device, const PollingPlan &plan);
} // namespace linkerbot::hand::unified_detail

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

#define LINKERBOT_DECLARE_MODEL_ADAPTERS(DeviceType)                                                                   \
    [[nodiscard]] CommandOutcome submit_command(DeviceType &device, const CommandRequest &request);                    \
    [[nodiscard]] TelemetrySample read_telemetry(DeviceType &device, TelemetrySemantic semantic,                       \
                                                 std::chrono::milliseconds timeout);                                   \
    [[nodiscard]] CommandOutcome operate_device(DeviceType &device, DeviceOperation operation,                         \
                                                std::chrono::milliseconds timeout)

    LINKERBOT_DECLARE_MODEL_ADAPTERS(l6::L6);
    LINKERBOT_DECLARE_MODEL_ADAPTERS(l6::HopL6);
    LINKERBOT_DECLARE_MODEL_ADAPTERS(o6::O6);
    LINKERBOT_DECLARE_MODEL_ADAPTERS(l20lite::L20lite);
    LINKERBOT_DECLARE_MODEL_ADAPTERS(l20::L20);
    LINKERBOT_DECLARE_MODEL_ADAPTERS(l30::L30);
    LINKERBOT_DECLARE_MODEL_ADAPTERS(o30::O30);

#undef LINKERBOT_DECLARE_MODEL_ADAPTERS

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

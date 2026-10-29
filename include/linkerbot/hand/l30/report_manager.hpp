#pragma once

#include <chrono>
#include <cstdint>
#include <set>

#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/l30/events.hpp>

namespace linkerbot::hand::l30 {

    // Device-side periodic reporting.
    //
    // Once a source is enabled the device pushes it on its own schedule, and the
    // frames land in the matching manager's snapshot and event sink exactly as a
    // blocking read would. Current limits and tactile matrices cannot be reported.
    class ReportManager {
        friend class L30;

    public:
        explicit ReportManager(Client &client);

        ReportManager(const ReportManager &) = delete;
        ReportManager &operator=(const ReportManager &) = delete;
        ReportManager(ReportManager &&) = delete;
        ReportManager &operator=(ReportManager &&) = delete;

        // period_ms must be within [20, 600000]. joint_mask selects joints by
        // bit; only the all-joints mask (0) is supported here, because the
        // manager snapshots are fixed 17-entry arrays.
        void configure(SensorSource source, bool enabled = true, uint32_t period_ms = protocol::kPeriodicMinPeriodMs,
                       uint32_t joint_mask = protocol::kPeriodicAllJointsMask,
                       std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        void disable(SensorSource source, std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        // Disables every source this manager enabled, in one pass.
        void disable_all(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        // The SDK default: angle reports at the minimum period.
        void start_default(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

    private:
        Client *client_;
        std::set<SensorSource> enabled_sources_;
    };

} // namespace linkerbot::hand::l30

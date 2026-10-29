#pragma once

#include <linkerbot/hand/api/readings.hpp>
#include <linkerbot/hand/capabilities.hpp>

namespace linkerbot::hand::unified_detail {
    inline void complete_freshness(SampleMeta &meta, const TelemetryCapability *channel,
                                   std::chrono::steady_clock::time_point now) {
        meta.age.reset();
        if (meta.collected_at && now >= *meta.collected_at) {
            meta.age = now - *meta.collected_at;
        }
        meta.stale = !meta.valid || (meta.age && channel && channel->freshness_ms &&
                                     *meta.age > std::chrono::duration<double, std::milli>{*channel->freshness_ms});
    }
}

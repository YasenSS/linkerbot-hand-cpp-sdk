#pragma once

#include <chrono>
#include <map>
#include <memory>

#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/api/events.hpp>
#include <linkerbot/hand/api/snapshot.hpp>

namespace linkerbot::hand::unified_detail {
    void publish_snapshot_events(const HandSnapshot &snapshot, const std::shared_ptr<EventQueue<HandEvent>> &queue,
                                 std::map<TelemetrySemantic, std::chrono::steady_clock::time_point> &last_seen);
} // namespace linkerbot::hand::unified_detail

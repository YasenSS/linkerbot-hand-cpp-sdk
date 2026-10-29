#include "linkerbot/hand/o30/motion.hpp"
#include <linkerbot/hand/errors.hpp>

#include "u8_io.hpp"

#include <stdexcept>
#include <utility>

#include <linkerbot/hand/o30/motion_manager.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    std::array<uint32_t, kJointCount> MotionTimeData::milliseconds() const {
        std::array<uint32_t, kJointCount> values{};
        for (size_t i = 0; i < kJointCount; ++i) {
            values[i] = static_cast<uint32_t>(counts[i]) * protocol::kMotionTimeMsPerCount;
        }
        return values;
    }

    MotionManager::MotionManager(Client &client) : client_(&client), relay_(client.runtime(), "motion/relay_") {}

    void MotionManager::set_counts(const std::array<uint8_t, kJointCount> &counts,
                                   std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiMotionTime, counts, timeout_ms);
    }

    void MotionManager::set_all_counts(uint8_t count, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_all(*client_, protocol::kMiMotionTime, count, timeout_ms);
    }

    void MotionManager::set_milliseconds(const std::array<uint32_t, kJointCount> &milliseconds,
                                         std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        std::array<uint8_t, kJointCount> counts{};
        for (size_t i = 0; i < kJointCount; ++i) {
            if (milliseconds[i] > protocol::kMotionTimeMaxMs) {
                throw ValidationError("motion time must be between 0 and 2550 ms");
            }
            const auto rounded =
                (milliseconds[i] + protocol::kMotionTimeMsPerCount / 2) / protocol::kMotionTimeMsPerCount;
            counts[i] = static_cast<uint8_t>(rounded);
        }
        set_counts(counts, timeout_ms);
    }

    MotionTimeData MotionManager::get_blocking(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        MotionTimeData data{};
        data.counts = detail::read_joints(*client_, protocol::kMiMotionTime, kind, timeout_ms);
        data.kind = kind;
        data.timestamp = client_->received_at();
        relay_.push(data);
        return data;
    }

    std::optional<MotionTimeData> MotionManager::get_snapshot() const { return relay_.snapshot(); }

    void MotionManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void MotionManager::set_event_sink(std::function<void(const MotionTimeData &)> sink) {
        relay_.set_sink(std::move(sink));
    }

} // namespace linkerbot::hand::o30

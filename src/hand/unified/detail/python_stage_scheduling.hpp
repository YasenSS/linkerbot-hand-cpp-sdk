#pragma once

#include <linkerbot/hand/capabilities.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>

namespace linkerbot::hand::unified_detail {
    // Fixed Python actor bound, counted in logical transactions per boundary,
    // including failed nested transactions, not in physical frames.
    inline constexpr unsigned kMaxInterstageAngleTransactions = 4;

    inline std::chrono::microseconds python_next_tactile_stage(Model model) {
        using namespace std::chrono_literals;
        // Python FORCE estimated_transaction_s / 5; O30's dynamic HOP stage.
        switch (model) {
        case Model::kL6:
            return 11000us;
        case Model::kO6:
            return 2400us;
        case Model::kL20Lite:
        case Model::kL20:
            return 4000us;
        case Model::kO30:
            return 3000us;
        default:
            return 0us; // No yielding contract (including L30 and L6 HOP).
        }
    }

    inline bool python_stage_has_budget(runtime::Deadline now, runtime::Deadline deadline,
                                        std::chrono::microseconds next_stage) {
        // Ordinary Python reads have independent response windows and no total
        // caller budget. A finite recovery execution deadline is never extended.
        return next_stage.count() > 0 && now < deadline && deadline - now > next_stage;
    }
}

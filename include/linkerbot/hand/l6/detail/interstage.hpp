#include <linkerbot/hand/errors.hpp>
#pragma once

#include <algorithm>
#include <chrono>
#include <linkerbot/hand/l6/runtime.hpp>
#include <optional>
#include <stdexcept>

namespace linkerbot::hand::l6::detail {
    using ScheduleClock = std::chrono::steady_clock;

    enum class BoundaryAction { ContinueTactile, QueryAngle, WaitForAngle, BudgetExhausted };

    inline bool adaptive_requires_query(const std::optional<ScheduleClock::time_point> &last_command_feedback,
                                        ScheduleClock::time_point angle_due) {
        return !last_command_feedback || *last_command_feedback < angle_due;
    }

    inline void validate_interstage(const InterstageOptions &options) {
        if (options.max_angle_transactions > 64 || options.finger_estimate.count() <= 0 ||
            options.timing_margin.count() < 0 || options.angle_budget < std::chrono::milliseconds{1} ||
            options.request_timeout < std::chrono::milliseconds{1}) {
            throw ValidationError("invalid L6 interstage scheduling options");
        }
    }

    // Pure decision: no clocks, sleeps, I/O or deadline mutation. Both layers
    // use this rule; deterministic tests exercise phase crossings and budgets.
    inline BoundaryAction boundary_action(const InterstageOptions &options, ScheduleClock::time_point now,
                                          ScheduleClock::time_point angle_due, std::chrono::microseconds angle_period,
                                          ScheduleClock::time_point service_deadline, size_t remaining_fingers,
                                          unsigned dispatched) {
        if (dispatched >= options.max_angle_transactions) {
            return BoundaryAction::ContinueTactile;
        }
        const auto reserve = (options.finger_estimate + options.timing_margin) * remaining_fingers;
        const bool due = now >= angle_due;
        const bool wait = !due && options.query_lookahead &&
                          now + options.finger_estimate + options.timing_margin >= angle_due + angle_period;
        if (!due && !wait) {
            return BoundaryAction::ContinueTactile;
        }
        const auto dispatch_at = due ? now : angle_due;
        if (dispatch_at + options.angle_budget + reserve > service_deadline) {
            return BoundaryAction::BudgetExhausted;
        }
        return due ? BoundaryAction::QueryAngle : BoundaryAction::WaitForAngle;
    }
} // namespace linkerbot::hand::l6::detail

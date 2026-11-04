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
    namespace argument_detail {
        // Keep on-wire identifiers, selectors and lengths in one named record.
        struct BoundaryActionArguments {
            const InterstageOptions &options;
            ScheduleClock::time_point now;
            ScheduleClock::time_point angle_due;
            std::chrono::microseconds angle_period;
            ScheduleClock::time_point service_deadline;
            size_t remaining_fingers;
            unsigned dispatched;
        };
        inline BoundaryAction boundary_action(const BoundaryActionArguments &arguments) {
            if (arguments.dispatched >= arguments.options.max_angle_transactions) {
                return BoundaryAction::ContinueTactile;
            }
            const auto reserve =
                (arguments.options.finger_estimate + arguments.options.timing_margin) * arguments.remaining_fingers;
            const bool due = arguments.now >= arguments.angle_due;
            const bool wait = !due && arguments.options.query_lookahead &&
                              arguments.now + arguments.options.finger_estimate + arguments.options.timing_margin >=
                                  arguments.angle_due + arguments.angle_period;
            if (!due && !wait) {
                return BoundaryAction::ContinueTactile;
            }
            const auto dispatch_at = due ? arguments.now : arguments.angle_due;
            if (dispatch_at + arguments.options.angle_budget + reserve > arguments.service_deadline) {
                return BoundaryAction::BudgetExhausted;
            }
            return due ? BoundaryAction::QueryAngle : BoundaryAction::WaitForAngle;
        }
    }
    inline BoundaryAction boundary_action(const InterstageOptions &options, ScheduleClock::time_point now,
                                          ScheduleClock::time_point angle_due, std::chrono::microseconds angle_period,
                                          ScheduleClock::time_point service_deadline, size_t remaining_fingers,
                                          unsigned dispatched) {
        return argument_detail::boundary_action(
            {options, now, angle_due, angle_period, service_deadline, remaining_fingers, dispatched});
    }
} // namespace linkerbot::hand::l6::detail

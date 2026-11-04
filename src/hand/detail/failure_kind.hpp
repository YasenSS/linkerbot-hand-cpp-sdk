#pragma once

#include <exception>
#include <linkerbot/hand/errors.hpp>

namespace linkerbot::hand::detail {
    enum class FailureKind { timeout, lifecycle, closed, other };
    inline FailureKind failure_kind(std::exception_ptr error) noexcept {
        if (!error) {
            return FailureKind::other;
        }
        try {
            std::rethrow_exception(error);
        } catch (const TimeoutError &) {
            return FailureKind::timeout;
        } catch (const LifecycleError &) {
            return FailureKind::lifecycle;
        } catch (const ClosedError &) {
            return FailureKind::closed;
        } catch (...) {
            return FailureKind::other;
        }
    }
}

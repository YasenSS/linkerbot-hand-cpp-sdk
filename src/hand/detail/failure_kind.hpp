#pragma once

#include <exception>
#include <linkerbot/hand/errors.hpp>

namespace linkerbot::hand::detail {
    // Nested protocol failures belong to their own waiter/handle. Connection
    // and generation failures must also stop an enclosing staged transaction.
    inline void rethrow_nested_connection_failure(const std::exception_ptr &error) {
        if (!error) {
            return;
        }
        try {
            std::rethrow_exception(error);
        } catch (const TransportError &) {
            throw;
        } catch (const DeviceNotReadyError &) {
            throw;
        } catch (const ClosedError &) {
            throw;
        } catch (...) {
            // The inner operation has already retained its ordinary failure.
            return;
        }
    }
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

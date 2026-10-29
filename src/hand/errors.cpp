#include <linkerbot/hand/errors.hpp>

#include <linkerbot/can/errors.hpp>
#include <linkerbot/transport/cancellation.hpp>

#include <type_traits>

namespace linkerbot::hand {
    namespace {
        std::exception_ptr make_error(ErrorCode code, std::string message, ErrorContext context,
                                      std::exception_ptr cause) {
            switch (code) {
            case ErrorCode::kValidation:
                return std::make_exception_ptr(
                    ValidationError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kUnsupportedOperation:
                return std::make_exception_ptr(
                    UnsupportedOperationError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kUnsupportedFirmware:
                return std::make_exception_ptr(
                    UnsupportedFirmwareError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kIdentityMismatch:
                return std::make_exception_ptr(
                    IdentityMismatchError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kDeviceNotReady:
                return std::make_exception_ptr(
                    DeviceNotReadyError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kTransport:
                return std::make_exception_ptr(
                    TransportError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kProtocol:
                return std::make_exception_ptr(ProtocolError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kTimeout:
                return std::make_exception_ptr(TimeoutError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kDeviceFault:
                return std::make_exception_ptr(
                    DeviceFaultError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kQueueFull:
                return std::make_exception_ptr(
                    QueueFullError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kPollingPlan:
                return std::make_exception_ptr(
                    PollingPlanError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kClosed:
                return std::make_exception_ptr(ClosedError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kLifecycle:
                return std::make_exception_ptr(
                    LifecycleError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kInternal:
                return std::make_exception_ptr(InternalError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kDiscovery:
                return std::make_exception_ptr(
                    DiscoveryError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kNoHandFound:
                return std::make_exception_ptr(
                    NoHandFoundError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kAmbiguousHand:
                return std::make_exception_ptr(
                    AmbiguousHandError(std::move(message), std::move(context), std::move(cause)));
            case ErrorCode::kIncompleteIdentity:
                return std::make_exception_ptr(
                    IncompleteIdentityError(std::move(message), std::move(context), std::move(cause)));
            }
            return std::make_exception_ptr(InternalError(std::move(message), std::move(context), std::move(cause)));
        }

        ErrorCode classify_can_error(const can::CANError &error) {
            switch (error.code()) {
            case can::ErrorCode::kValidation:
                return ErrorCode::kValidation;
            case can::ErrorCode::kClosed:
                return ErrorCode::kClosed;
            case can::ErrorCode::kTimeout:
                return ErrorCode::kTimeout;
            case can::ErrorCode::kTransportIo:
                return ErrorCode::kTransport;
            case can::ErrorCode::kFaulted:
                return ErrorCode::kTransport;
            case can::ErrorCode::kQueueFull:
                return ErrorCode::kQueueFull;
            case can::ErrorCode::kInternal:
                return ErrorCode::kInternal;
            }
            return ErrorCode::kInternal;
        }
    }

    std::exception_ptr classify_exception(std::exception_ptr cause, ErrorContext context, ErrorCode fallback) {
        if (!cause) {
            return make_error(ErrorCode::kInternal, "empty exception", std::move(context), {});
        }

        try {
            std::rethrow_exception(cause);
        } catch (HandError &error) {
            error.enrich_context(context);
            // MSVC rethrow_exception may throw a copy. Capture the enriched
            // active exception instead of returning the original stored copy.
            return std::current_exception();
        } catch (const can::CANError &error) {
            if (!context.native_status && error.native_status()) {
                context.native_status = error.native_status();
            }
            return make_error(classify_can_error(error), error.what(), std::move(context), std::move(cause));
        } catch (const transport::CancelledError &error) {
            return make_error(ErrorCode::kLifecycle, error.what(), std::move(context), std::move(cause));
        } catch (const std::invalid_argument &error) {
            return make_error(ErrorCode::kValidation, error.what(), std::move(context), std::move(cause));
        } catch (const std::exception &error) {
            return make_error(fallback, error.what(), std::move(context), std::move(cause));
        } catch (...) {
            return make_error(fallback, "non-standard exception", std::move(context), std::move(cause));
        }
    }

    void rethrow_as_hand_error(std::exception_ptr cause, ErrorContext context, ErrorCode fallback) {
        std::rethrow_exception(classify_exception(std::move(cause), std::move(context), fallback));
    }
}

#pragma once

#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace linkerbot::hand {
    // Increment when an existing error code or its machine-readable meaning
    // changes incompatibly. Adding a new append-only code keeps this version.
    inline constexpr uint16_t kErrorSchemaVersion = 1;

    // Stable, machine-readable categories for errors crossing the hand SDK API.
    enum class ErrorCode : uint16_t {
        kValidation = 1,
        kUnsupportedOperation = 2,
        kUnsupportedFirmware = 3,
        kIdentityMismatch = 4,
        kDeviceNotReady = 5,
        kTransport = 6,
        kProtocol = 7,
        kTimeout = 8,
        kDeviceFault = 9,
        kQueueFull = 10,
        kPollingPlan = 11,
        kClosed = 12,
        kLifecycle = 13,
        kInternal = 14,
        kDiscovery = 15,
        kNoHandFound = 16,
        kAmbiguousHand = 17,
        kIncompleteIdentity = 18,
    };

    [[nodiscard]] constexpr std::string_view error_code_name(ErrorCode code) noexcept {
        switch (code) {
        case ErrorCode::kValidation:
            return "validation";
        case ErrorCode::kUnsupportedOperation:
            return "unsupported_operation";
        case ErrorCode::kUnsupportedFirmware:
            return "unsupported_firmware";
        case ErrorCode::kIdentityMismatch:
            return "identity_mismatch";
        case ErrorCode::kDeviceNotReady:
            return "device_not_ready";
        case ErrorCode::kTransport:
            return "transport";
        case ErrorCode::kProtocol:
            return "protocol";
        case ErrorCode::kTimeout:
            return "timeout";
        case ErrorCode::kDeviceFault:
            return "device_fault";
        case ErrorCode::kQueueFull:
            return "queue_full";
        case ErrorCode::kPollingPlan:
            return "polling_plan";
        case ErrorCode::kClosed:
            return "closed";
        case ErrorCode::kLifecycle:
            return "lifecycle";
        case ErrorCode::kInternal:
            return "internal";
        case ErrorCode::kDiscovery:
            return "discovery";
        case ErrorCode::kNoHandFound:
            return "no_hand_found";
        case ErrorCode::kAmbiguousHand:
            return "ambiguous_hand";
        case ErrorCode::kIncompleteIdentity:
            return "incomplete_identity";
        }
        return "unknown";
    }

    struct ErrorContext {
        std::string operation;
        std::string model;
        std::string channel;
        uint64_t generation = 0;
        std::optional<uint32_t> arbitration_id;
        std::optional<int> native_status;
    };

    class HandError : public std::runtime_error {
    public:
        HandError(ErrorCode code, std::string message, ErrorContext context = {}, std::exception_ptr cause = {})
            : std::runtime_error(std::move(message)), code_(code), context_(std::move(context)),
              cause_(std::move(cause)) {}

        [[nodiscard]] ErrorCode code() const noexcept { return code_; }

        [[nodiscard]] const ErrorContext &context() const noexcept { return context_; }

        [[nodiscard]] std::exception_ptr cause() const noexcept { return cause_; }

    private:
        void enrich_context(const ErrorContext &context) {
            if (context_.operation.empty()) {
                context_.operation = context.operation;
            }
            if (context_.model.empty()) {
                context_.model = context.model;
            }
            if (context_.channel.empty()) {
                context_.channel = context.channel;
            }
            if (context_.generation == 0) {
                context_.generation = context.generation;
            }
            if (!context_.arbitration_id) {
                context_.arbitration_id = context.arbitration_id;
            }
            if (!context_.native_status) {
                context_.native_status = context.native_status;
            }
        }

        friend std::exception_ptr classify_exception(std::exception_ptr cause, ErrorContext context,
                                                     ErrorCode fallback);

        ErrorCode code_;
        ErrorContext context_;
        std::exception_ptr cause_;
    };

#define LINKERBOT_HAND_ERROR_TYPE(Name, Code)                                                                          \
    class Name : public HandError {                                                                                    \
    public:                                                                                                            \
        explicit Name(std::string message, ErrorContext context = {}, std::exception_ptr cause = {})                   \
            : HandError(Code, std::move(message), std::move(context), std::move(cause)) {}                             \
    }

    LINKERBOT_HAND_ERROR_TYPE(ValidationError, ErrorCode::kValidation);
    LINKERBOT_HAND_ERROR_TYPE(UnsupportedOperationError, ErrorCode::kUnsupportedOperation);
    LINKERBOT_HAND_ERROR_TYPE(UnsupportedFirmwareError, ErrorCode::kUnsupportedFirmware);
    LINKERBOT_HAND_ERROR_TYPE(IdentityMismatchError, ErrorCode::kIdentityMismatch);
    LINKERBOT_HAND_ERROR_TYPE(DeviceNotReadyError, ErrorCode::kDeviceNotReady);
    LINKERBOT_HAND_ERROR_TYPE(TransportError, ErrorCode::kTransport);
    LINKERBOT_HAND_ERROR_TYPE(ProtocolError, ErrorCode::kProtocol);
    LINKERBOT_HAND_ERROR_TYPE(TimeoutError, ErrorCode::kTimeout);
    LINKERBOT_HAND_ERROR_TYPE(DeviceFaultError, ErrorCode::kDeviceFault);
    LINKERBOT_HAND_ERROR_TYPE(QueueFullError, ErrorCode::kQueueFull);
    LINKERBOT_HAND_ERROR_TYPE(PollingPlanError, ErrorCode::kPollingPlan);
    LINKERBOT_HAND_ERROR_TYPE(ClosedError, ErrorCode::kClosed);
    LINKERBOT_HAND_ERROR_TYPE(LifecycleError, ErrorCode::kLifecycle);
    LINKERBOT_HAND_ERROR_TYPE(InternalError, ErrorCode::kInternal);

#undef LINKERBOT_HAND_ERROR_TYPE

    class DiscoveryError : public HandError {
    public:
        explicit DiscoveryError(std::string message, ErrorContext context = {}, std::exception_ptr cause = {})
            : DiscoveryError(ErrorCode::kDiscovery, std::move(message), std::move(context), std::move(cause)) {}

    protected:
        DiscoveryError(ErrorCode code, std::string message, ErrorContext context, std::exception_ptr cause)
            : HandError(code, std::move(message), std::move(context), std::move(cause)) {}
    };

    class NoHandFoundError : public DiscoveryError {
    public:
        explicit NoHandFoundError(std::string message, ErrorContext context = {}, std::exception_ptr cause = {})
            : DiscoveryError(ErrorCode::kNoHandFound, std::move(message), std::move(context), std::move(cause)) {}
    };

    class AmbiguousHandError : public DiscoveryError {
    public:
        explicit AmbiguousHandError(std::string message, ErrorContext context = {}, std::exception_ptr cause = {})
            : DiscoveryError(ErrorCode::kAmbiguousHand, std::move(message), std::move(context), std::move(cause)) {}
    };

    class IncompleteIdentityError : public DiscoveryError {
    public:
        explicit IncompleteIdentityError(std::string message, ErrorContext context = {}, std::exception_ptr cause = {})
            : DiscoveryError(ErrorCode::kIncompleteIdentity, std::move(message), std::move(context), std::move(cause)) {
        }
    };

    // Compatibility names used by the existing identity parser.
    class IdentityProtocolError : public ProtocolError {
    public:
        using ProtocolError::ProtocolError;
    };

    // Normalize exceptions crossing from transport, protocol workers, or
    // legacy model implementations into the public hand error hierarchy.
    [[nodiscard]] std::exception_ptr classify_exception(std::exception_ptr cause, ErrorContext context = {},
                                                        ErrorCode fallback = ErrorCode::kInternal);

    [[noreturn]] void rethrow_as_hand_error(std::exception_ptr cause, ErrorContext context = {},
                                            ErrorCode fallback = ErrorCode::kInternal);
}

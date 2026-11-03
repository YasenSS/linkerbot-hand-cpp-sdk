#include <catch2/catch_test_macros.hpp>

#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/transport/cancellation.hpp>

#include <stdexcept>

using namespace linkerbot::hand;

TEST_CASE("hand errors expose stable code and context", "[errors]") {
    ErrorContext context;
    context.operation = "get_angles";
    context.model = "O6";
    context.channel = "angles";
    context.generation = 7;
    context.arbitration_id = 0x27;
    context.native_status = 110;

    TimeoutError error("angle query timed out", context);

    REQUIRE(error.code() == ErrorCode::kTimeout);
    REQUIRE(error.what() == std::string("angle query timed out"));
    REQUIRE(error.context().operation == "get_angles");
    REQUIRE(error.context().model == "O6");
    REQUIRE(error.context().channel == "angles");
    REQUIRE(error.context().generation == 7);
    REQUIRE(error.context().arbitration_id == 0x27);
    REQUIRE(error.context().native_status == 110);
    REQUIRE(dynamic_cast<const HandError *>(&error) != nullptr);
    REQUIRE(dynamic_cast<const std::runtime_error *>(&error) != nullptr);
}

TEST_CASE("hand errors preserve an underlying cause", "[errors]") {
    std::exception_ptr cause;
    try {
        throw std::runtime_error("socket read failed");
    } catch (...) {
        cause = std::current_exception();
    }

    TransportError error("transport failed", {}, cause);

    REQUIRE(error.code() == ErrorCode::kTransport);
    REQUIRE(error.cause() != nullptr);
    REQUIRE_THROWS_AS(std::rethrow_exception(error.cause()), std::runtime_error);
}

TEST_CASE("hand error subclasses assign their category", "[errors]") {
    REQUIRE(ValidationError("bad input").code() == ErrorCode::kValidation);
    REQUIRE(UnsupportedOperationError("unsupported").code() == ErrorCode::kUnsupportedOperation);
    REQUIRE(UnsupportedFirmwareError("old firmware").code() == ErrorCode::kUnsupportedFirmware);
    REQUIRE(IdentityMismatchError("wrong model").code() == ErrorCode::kIdentityMismatch);
    REQUIRE(DeviceNotReadyError("not ready").code() == ErrorCode::kDeviceNotReady);
    REQUIRE(ProtocolError("bad response").code() == ErrorCode::kProtocol);
    REQUIRE(DeviceFaultError("fault").code() == ErrorCode::kDeviceFault);
    REQUIRE(QueueFullError("full").code() == ErrorCode::kQueueFull);
    REQUIRE(PollingPlanError("invalid plan").code() == ErrorCode::kPollingPlan);
    REQUIRE(ClosedError("closed").code() == ErrorCode::kClosed);
    REQUIRE(LifecycleError("invalid lifecycle").code() == ErrorCode::kLifecycle);
    REQUIRE(InternalError("internal").code() == ErrorCode::kInternal);
}

TEST_CASE("hand error code names are stable", "[errors]") {
    REQUIRE(error_code_name(ErrorCode::kValidation) == "validation");
    REQUIRE(error_code_name(ErrorCode::kUnsupportedOperation) == "unsupported_operation");
    REQUIRE(error_code_name(ErrorCode::kUnsupportedFirmware) == "unsupported_firmware");
    REQUIRE(error_code_name(ErrorCode::kIdentityMismatch) == "identity_mismatch");
    REQUIRE(error_code_name(ErrorCode::kDeviceNotReady) == "device_not_ready");
    REQUIRE(error_code_name(ErrorCode::kTransport) == "transport");
    REQUIRE(error_code_name(ErrorCode::kProtocol) == "protocol");
    REQUIRE(error_code_name(ErrorCode::kTimeout) == "timeout");
    REQUIRE(error_code_name(ErrorCode::kDeviceFault) == "device_fault");
    REQUIRE(error_code_name(ErrorCode::kQueueFull) == "queue_full");
    REQUIRE(error_code_name(ErrorCode::kPollingPlan) == "polling_plan");
    REQUIRE(error_code_name(ErrorCode::kClosed) == "closed");
    REQUIRE(error_code_name(ErrorCode::kLifecycle) == "lifecycle");
    REQUIRE(error_code_name(ErrorCode::kInternal) == "internal");
    // Fixed-underlying-type enum values are valid C++17, including unknown values and bit masks.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    REQUIRE(error_code_name(static_cast<ErrorCode>(0)) == "unknown");
}

TEST_CASE("identity compatibility errors use hand error categories", "[errors][identity]") {
    const IdentityProtocolError protocol("invalid identity");
    const IdentityMismatchError mismatch("wrong identity");

    REQUIRE(protocol.code() == ErrorCode::kProtocol);
    REQUIRE(mismatch.code() == ErrorCode::kIdentityMismatch);
    REQUIRE(dynamic_cast<const HandError *>(&protocol) != nullptr);
    REQUIRE(dynamic_cast<const HandError *>(&mismatch) != nullptr);
}

TEST_CASE("transport exceptions normalize into hand categories", "[errors][mapping]") {
    auto classify = [](std::exception_ptr source, ErrorContext context = {}) {
        return classify_exception(std::move(source), std::move(context));
    };

    auto timeout = classify(std::make_exception_ptr(linkerbot::can::TimeoutError("can timeout")));
    REQUIRE_THROWS_AS(std::rethrow_exception(timeout), TimeoutError);

    auto queue_full = classify(std::make_exception_ptr(linkerbot::can::QueueFullError("queue full")));
    REQUIRE_THROWS_AS(std::rethrow_exception(queue_full), QueueFullError);

    auto closed = classify(std::make_exception_ptr(linkerbot::can::ClosedError("can closed")));
    REQUIRE_THROWS_AS(std::rethrow_exception(closed), ClosedError);

    auto cancelled = classify(std::make_exception_ptr(linkerbot::transport::CancelledError("cancelled")));
    REQUIRE_THROWS_AS(std::rethrow_exception(cancelled), LifecycleError);

    auto invalid = classify(std::make_exception_ptr(std::invalid_argument("bad argument")));
    REQUIRE_THROWS_AS(std::rethrow_exception(invalid), ValidationError);
}

TEST_CASE("transport mapping retains context, native status, and cause", "[errors][mapping]") {
    ErrorContext context;
    context.operation = "send_frame";
    context.model = "L30";
    context.channel = "command";

    auto source = std::make_exception_ptr(linkerbot::can::TransportIOError("write failed", {}, 5));
    auto normalized = classify_exception(source, context);

    try {
        std::rethrow_exception(normalized);
        FAIL("expected normalized transport error");
    } catch (const TransportError &error) {
        REQUIRE(error.code() == ErrorCode::kTransport);
        REQUIRE(error.context().operation == "send_frame");
        REQUIRE(error.context().model == "L30");
        REQUIRE(error.context().channel == "command");
        REQUIRE(error.context().native_status == 5);
        REQUIRE(error.cause() != nullptr);
        REQUIRE_THROWS_AS(std::rethrow_exception(error.cause()), linkerbot::can::TransportIOError);
    }
}

TEST_CASE("generic exceptions use the requested fallback category", "[errors][mapping]") {
    auto source = std::make_exception_ptr(std::runtime_error("malformed reply"));
    auto normalized = classify_exception(source, {}, ErrorCode::kProtocol);

    REQUIRE_THROWS_AS(std::rethrow_exception(normalized), ProtocolError);
}

TEST_CASE("existing hand errors retain their dynamic type and gain boundary context", "[errors][mapping]") {
    ErrorContext context;
    context.operation = "runtime.decode";
    context.model = "O30";
    context.channel = "angles";
    context.generation = 3;
    context.arbitration_id = 0x401;

    auto source = std::make_exception_ptr(ProtocolError("malformed response"));
    auto normalized = classify_exception(source, context);

    try {
        std::rethrow_exception(normalized);
        FAIL("expected protocol error");
    } catch (const ProtocolError &error) {
        REQUIRE(error.code() == ErrorCode::kProtocol);
        REQUIRE(error.context().operation == "runtime.decode");
        REQUIRE(error.context().model == "O30");
        REQUIRE(error.context().channel == "angles");
        REQUIRE(error.context().generation == 3);
        REQUIRE(error.context().arbitration_id == 0x401);
    }
}

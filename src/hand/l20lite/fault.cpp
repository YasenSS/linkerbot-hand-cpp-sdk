#include "linkerbot/hand/l20lite/fault.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20lite/fault_manager.hpp>
#include <linkerbot/relay.hpp>
#include <optional>
#include <stdexcept>
#include <utility>

namespace linkerbot::hand::l20lite {

    FaultManager::FaultManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher)
        : FaultManager(arbitration_id,
                       runtime::DeviceRuntime::borrow(dispatcher, "l20lite/" + std::to_string(arbitration_id),
                                                      {{arbitration_id, false}})) {}

    FaultManager::FaultManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          channel_(runtime_, "fault") {}

    FaultManager::~FaultManager() = default;

    void FaultManager::start() { channel_.start(); }

    void FaultManager::stop() { channel_.stop(); }

    FaultData FaultManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        const auto deadline = runtime::deadline_after(timeout_ms);
        return channel_.get(protocol_.make_read("fault"), deadline);
    }

    std::optional<FaultData> FaultManager::get_snapshot() const { return channel_.snapshot(); }

    void FaultManager::set_event_sink(std::function<void(const FaultData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

    void FaultManager::send_fault_request() {
        channel_.request(protocol_.make_read("fault"), runtime::deadline_after(std::chrono::milliseconds(100)));
    }

} // namespace linkerbot::hand::l20lite

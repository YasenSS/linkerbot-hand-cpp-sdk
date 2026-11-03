#include "linkerbot/hand/l20lite/force_sensor.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20lite/force_sensor_manager.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace linkerbot::hand::l20lite {
    namespace {
        constexpr std::array<const char *, 5> kFingerNames{"thumb", "index", "middle", "ring", "pinky"};
    } // namespace

    // --- SingleForceSensorManager ---

    SingleForceSensorManager::SingleForceSensorManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher,
                                                       uint8_t command_prefix)
        : SingleForceSensorManager(arbitration_id,
                                   runtime::DeviceRuntime::borrow(dispatcher,
                                                                  "l20lite/" + std::to_string(arbitration_id),
                                                                  {{arbitration_id, false}}),
                                   command_prefix) {}

    SingleForceSensorManager::SingleForceSensorManager(uint32_t arbitration_id,
                                                       std::shared_ptr<runtime::DeviceRuntime> runtime,
                                                       uint8_t command_prefix)
        : arbitration_id_(arbitration_id),
          operation_("force_sensor_" + std::string(kFingerNames.at(command_prefix - 0xB1))),
          command_prefix_(command_prefix), runtime_(std::move(runtime)), protocol_(arbitration_id),
          channel_(runtime_, operation_) {}

    SingleForceSensorManager::~SingleForceSensorManager() = default;

    void SingleForceSensorManager::start() { channel_.start(); }

    void SingleForceSensorManager::cancel_pending() { channel_.cancel(); }

    void SingleForceSensorManager::stop() { channel_.stop(); }

    ForceSensorData SingleForceSensorManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        const auto deadline = runtime::deadline_after(timeout_ms);
        return channel_.get(protocol_.make_read(operation_), deadline);
    }

    std::optional<ForceSensorData> SingleForceSensorManager::get_snapshot() const { return channel_.snapshot(); }

    void SingleForceSensorManager::set_event_sink(std::function<void(const ForceSensorData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

    void SingleForceSensorManager::send_request() {
        channel_.request(protocol_.make_read(operation_), runtime::deadline_after(std::chrono::milliseconds(100)));
    }

    // --- ForceSensorManager ---

    ForceSensorManager::ForceSensorManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher)
        : ForceSensorManager(arbitration_id,
                             runtime::DeviceRuntime::borrow(dispatcher, "l20lite/" + std::to_string(arbitration_id),
                                                            {{arbitration_id, false}})) {}

    ForceSensorManager::ForceSensorManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          channel_(runtime_, "force_sensor_all", true) {
        for (size_t i = 0; i < kFingerNames.size(); ++i) {
            fingers_.emplace(kFingerNames[i], std::make_unique<SingleForceSensorManager>(
                                                  arbitration_id, runtime_, static_cast<uint8_t>(0xB1 + i)));
        }
    }

    ForceSensorManager::~ForceSensorManager() {
        try {
            cancel_pending();
            // Destruction is nonthrowing; explicit close/stop remains the error-reporting path.
            // NOLINTNEXTLINE(bugprone-empty-catch)
        } catch (...) {
        }
    }

    void ForceSensorManager::start() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
        channel_.start();
        for (auto &[name, sensor] : fingers_) {
            sensor->start();
        }
    }

    void ForceSensorManager::cancel_pending() {
        channel_.cancel();
        for (auto &[name, sensor] : fingers_) {
            sensor->cancel_pending();
        }
    }

    void ForceSensorManager::stop() {
        runtime_->assert_external_lifecycle();
        const std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
        cancel_pending();
        for (auto &[name, sensor] : fingers_) {
            sensor->stop();
        }
        channel_.stop();
    }

    // One transaction, one absolute deadline, transaction-local 5-finger
    // assembler (see protocol::read_force_sensor_all): the aggregate is
    // published only when all 5 fingers' 12 rows arrive intact, so a
    // partial/timed-out batch never overwrites the last complete snapshot
    // and there is only ever one Request in flight (nothing to cancel on
    // partial failure by construction).
    AllFingersData ForceSensorManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        return channel_.get(protocol_.make_read("force_sensor_all"), runtime::deadline_after(timeout_ms));
    }

    std::optional<AllFingersData> ForceSensorManager::get_snapshot() const { return channel_.snapshot(); }

    SingleForceSensorManager &ForceSensorManager::get_finger(const std::string &name) {
        auto it = fingers_.find(name);
        if (it == fingers_.end()) {
            throw ValidationError("Unknown finger: " + name);
        }
        return *it->second;
    }

    void ForceSensorManager::send_sense_request() {
        channel_.request(protocol_.make_read("force_sensor_all"),
                         runtime::deadline_after(std::chrono::milliseconds(100)));
    }

    void ForceSensorManager::set_event_sink(std::function<void(const AllFingersData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

} // namespace linkerbot::hand::l20lite

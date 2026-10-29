#include "linkerbot/hand/l30/report_manager.hpp"
#include <linkerbot/hand/errors.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace linkerbot::hand::l30 {

    namespace {
        uint8_t report_subcommand(SensorSource source) {
            switch (source) {
            case SensorSource::kAngle:
                return protocol::kSubPosition;
            case SensorSource::kCurrent:
                return protocol::kSubCurrent;
            case SensorSource::kSpeed:
                return protocol::kSubSpeed;
            case SensorSource::kTemperature:
                return protocol::kSubTemperature;
            case SensorSource::kFault:
                return protocol::kSubFault;
            case SensorSource::kForceSensor:
                break;
            }
            throw ValidationError("L30 does not support periodic reports for source " +
                                  std::string(sensor_source_name(source)));
        }
    } // namespace

    ReportManager::ReportManager(Client &client) : client_(&client) {}

    void ReportManager::configure(SensorSource source, bool enabled, uint32_t period_ms, uint32_t joint_mask,
                                  std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        const auto operation = client_->operation(timeout_ms);
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        if (enabled && joint_mask != protocol::kPeriodicAllJointsMask) {
            throw ValidationError("L30 snapshots only support all-joint periodic reports");
        }
        const auto subcmd = report_subcommand(source);
        const auto response = client_->request_ack(protocol::kParentPeriodic, subcmd,
                                                   protocol::encode_periodic_config(enabled, period_ms, joint_mask),
                                                   protocol::kPeriodicConfigWireBytes, operation.remaining());
        protocol::check_response_status(response.data(), response.size());
        if (enabled) {
            enabled_sources_.insert(source);
        } else {
            enabled_sources_.erase(source);
        }
    }

    void ReportManager::disable(SensorSource source, std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        configure(source, false, protocol::kPeriodicMinPeriodMs, protocol::kPeriodicAllJointsMask, timeout_ms);
    }

    void ReportManager::disable_all(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        const auto operation = client_->operation(timeout_ms);
        // Copy first: configure() erases from enabled_sources_ as it goes.
        const std::vector<SensorSource> sources(enabled_sources_.begin(), enabled_sources_.end());
        for (const auto source : sources) {
            disable(source, operation.remaining());
        }
    }

    void ReportManager::start_default(std::chrono::milliseconds timeout_ms) {
        const auto call = client_->enter();
        configure(SensorSource::kAngle, true, protocol::kPeriodicMinPeriodMs, protocol::kPeriodicAllJointsMask,
                  timeout_ms);
    }

} // namespace linkerbot::hand::l30

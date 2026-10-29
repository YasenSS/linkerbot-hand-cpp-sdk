#include "linkerbot/hand/l20/angle.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <linkerbot/can/errors.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20/angle_manager.hpp>
#include <linkerbot/relay.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace linkerbot::hand::l20 {
    namespace {
        std::vector<std::string> joint_name_list() {
            std::vector<std::string> names;
            names.reserve(kJointNames.size());
            for (auto name : kJointNames) {
                names.emplace_back(name);
            }
            return names;
        }
    } // namespace

    AngleManager::AngleManager(uint32_t arbitration_id, can::CANDispatcher &dispatcher,
                               const std::optional<std::filesystem::path> &angle_mapping_path, std::string_view side,
                               std::string_view interface_name)
        : AngleManager(arbitration_id,
                       runtime::DeviceRuntime::borrow(dispatcher, "l20/" + std::to_string(arbitration_id),
                                                      {{arbitration_id, false}}),
                       angle_mapping_path, side, interface_name) {}

    AngleManager::AngleManager(uint32_t arbitration_id, std::shared_ptr<runtime::DeviceRuntime> runtime,
                               const std::optional<std::filesystem::path> &angle_mapping_path, std::string_view side,
                               std::string_view interface_name)
        : arbitration_id_(arbitration_id), runtime_(std::move(runtime)), protocol_(arbitration_id),
          mapping_("l20", joint_name_list(), angle_mapping_path, std::string(side), std::string(interface_name)),
          channel_(runtime_, "angle") {}

    AngleManager::~AngleManager() = default;

    void AngleManager::start() { channel_.start(); }

    void AngleManager::stop() { channel_.stop(true); }

    void AngleManager::set_angles(const std::array<uint8_t, Angle::kJointCount> &angles) {
        channel_.request(protocol_.make_command("angle", angles), runtime::Deadline::max());
    }

    void AngleManager::set_angles_percent(const Angle &angles) {
        angles.validate();
        send_mapped_angles(percents_to_raw(angles.to_array()));
    }

    void AngleManager::set_angles_percent(const std::array<float, Angle::kJointCount> &values) {
        set_angles_percent(Angle::from_array(values));
    }

    void AngleManager::send_mapped_angles(const std::array<uint8_t, Angle::kJointCount> &raw_angles) {
        const auto mapped = mapping_.map_values({raw_angles.begin(), raw_angles.end()});
        std::array<uint8_t, Angle::kJointCount> device_raw{};
        std::copy(mapped.begin(), mapped.end(), device_raw.begin());
        set_angles(device_raw);
    }

    AngleMapping AngleManager::get_angle_mapping() const { return mapping_.get_mapping(); }

    AngleMapping AngleManager::get_default_angle_mapping() const { return mapping_.get_default_mapping(); }

    void AngleManager::set_angle_mapping(const AngleMapping &mapping) { mapping_.set_mapping(mapping); }

    void AngleManager::reset_angle_mapping() { mapping_.reset_mapping(); }

    AngleData AngleManager::get_blocking(std::chrono::milliseconds timeout_ms) {
        if (timeout_ms.count() <= 0) {
            throw ValidationError("timeout_ms must be positive");
        }
        const auto deadline = runtime::deadline_after(timeout_ms);
        return channel_.get(protocol_.make_read("angle"), deadline);
    }

    std::optional<AngleData> AngleManager::get_snapshot() const { return channel_.snapshot(); }

    void AngleManager::set_event_sink(std::function<void(const AngleData &)> sink) {
        channel_.set_sink(std::move(sink));
    }

    void AngleManager::send_sense_request() {
        channel_.request(protocol_.make_read("angle"), runtime::deadline_after(std::chrono::milliseconds(200)));
    }

} // namespace linkerbot::hand::l20

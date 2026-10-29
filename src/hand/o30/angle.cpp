#include "linkerbot/hand/o30/angle.hpp"
#include <linkerbot/hand/errors.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include <linkerbot/hand/o30/angle_manager.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

#include "u8_io.hpp"

namespace linkerbot::hand::o30 {

    Angle Angle::from_array(const std::array<float, kJointCount> &values) {
        validate_percents(values, "angles");
        return Angle{values};
    }

    Angle Angle::from_sensor_raw(const std::array<uint8_t, kJointCount> &raw) { return Angle{raw_to_percents(raw)}; }

    namespace {
        // Tips/root groups contain flexion joints only; other axes are converted
        // by the indexed full-pose path in percents_to_raw().
        template <size_t N> std::array<uint8_t, N> percents_to_group(const std::array<float, N> &percents) {
            for (auto value : percents) {
                if (!(value >= 0.0F) || !(value <= 100.0F)) {
                    throw ValidationError("group angle must be between 0 and 100");
                }
            }
            std::array<uint8_t, N> raw{};
            for (size_t i = 0; i < N; ++i) {
                raw[i] = percent_to_raw(percents[i]);
            }
            return raw;
        }

        template <size_t N>
        void write_group(Client &client, uint8_t si, const std::array<uint8_t, N> &raw,
                         std::chrono::milliseconds timeout) {
            protocol::require_positive_timeout(timeout);
            static_cast<void>(
                client.write(protocol::kMiPosition, si, std::vector<uint8_t>(raw.begin(), raw.end()), timeout));
        }
    } // namespace

    AngleManager::AngleManager(Client &client) : client_(&client), relay_(client.runtime(), "angle/relay_") {}

    void AngleManager::set_angles(const std::array<uint8_t, kJointCount> &angles,
                                  std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, protocol::kMiPosition, angles, timeout_ms);
    }

    void AngleManager::set_angles_percent(const Angle &angles, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        angles.validate();
        set_angles(angles.to_raw(), timeout_ms);
    }

    void AngleManager::set_angles_percent(const std::array<float, kJointCount> &percents,
                                          std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        set_angles_percent(Angle::from_array(percents), timeout_ms);
    }

    void AngleManager::set_tips(const std::array<float, kTipCount> &percents, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        set_raw_tips(percents_to_group(percents), timeout_ms);
    }

    void AngleManager::set_raw_tips(const std::array<uint8_t, kTipCount> &raw, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_group(*client_, kTipSi, raw, timeout_ms);
    }

    void AngleManager::set_roots1(const std::array<float, kRoot1Count> &percents,
                                  std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        set_raw_roots1(percents_to_group(percents), timeout_ms);
    }

    void AngleManager::set_raw_roots1(const std::array<uint8_t, kRoot1Count> &raw,
                                      std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_group(*client_, kRoot1Si, raw, timeout_ms);
    }

    void AngleManager::set_roots2(const std::array<float, kRoot2Count> &percents,
                                  std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        set_raw_roots2(percents_to_group(percents), timeout_ms);
    }

    void AngleManager::set_raw_roots2(const std::array<uint8_t, kRoot2Count> &raw,
                                      std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_group(*client_, kRoot2Si, raw, timeout_ms);
    }

    void AngleManager::set_sparse(const std::vector<std::pair<uint8_t, uint8_t>> &slot_values,
                                  std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        if (slot_values.empty()) {
            throw ValidationError("sparse position list must not be empty");
        }
        if (slot_values.size() * 2 > protocol::kMaxHopPayload) {
            throw ValidationError("sparse position list exceeds one HOP frame");
        }
        std::vector<uint8_t> payload;
        std::vector<std::pair<uint8_t, uint8_t>> dense;
        payload.reserve(slot_values.size() * 2);
        for (const auto &entry : slot_values) {
            const auto it = std::find(kJointSparseIds.begin(), kJointSparseIds.end(), entry.first);
            if (it == kJointSparseIds.end()) {
                throw ValidationError("sparse ID does not name a mounted O30i joint");
            }
            dense.emplace_back(kJointSlots[static_cast<size_t>(it - kJointSparseIds.begin())], entry.second);
            payload.push_back(entry.first);
            payload.push_back(entry.second);
        }
        if (client_->revision(timeout_ms) == protocol::Revision::kV004) {
            // 0.0.4 MI 0x30 was observed to move a different joint. Preserve
            // the public finger-major IDs using verified dense single-slot
            // writes; validate the entire list before changing any joint.
            for (const auto &entry : dense) {
                static_cast<void>(client_->write(protocol::kMiPosition, entry.first, {entry.second}, timeout_ms));
            }
            return;
        }
        static_cast<void>(client_->write(protocol::kMiSparsePosition, 0, payload, timeout_ms));
    }

    void AngleManager::set_sparse_joints(const std::vector<std::pair<size_t, uint8_t>> &joint_values,
                                         std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        std::vector<std::pair<uint8_t, uint8_t>> slots;
        slots.reserve(joint_values.size());
        for (const auto &entry : joint_values) {
            if (entry.first >= kJointCount) {
                throw ValidationError("sparse joint index is outside 0..19");
            }
            slots.emplace_back(kJointSparseIds[entry.first], entry.second);
        }
        set_sparse(slots, timeout_ms);
    }

    AngleData AngleManager::get_blocking(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        const auto raw = detail::read_joints(*client_, protocol::kMiPosition, kind, timeout_ms);
        const AngleData data{Angle::from_sensor_raw(raw), kind, client_->received_at()};
        relay_.push(data);
        return data;
    }

    std::array<uint8_t, kSlotCount> AngleManager::get_slots(std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        return detail::read_slots(*client_, protocol::kMiPosition, kind, timeout_ms);
    }

    HalfwordPositionData AngleManager::get_halfword(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        const auto payload = client_->read(protocol::kMiPosition16, 0, protocol::kU16ObjectBytes, false, timeout_ms);
        HalfwordPositionData data{};
        data.positions = project_u16(protocol::as_u16_le_slots(payload));
        data.timestamp = client_->received_at();
        return data;
    }

    std::optional<AngleData> AngleManager::get_snapshot() const { return relay_.snapshot(); }

    void AngleManager::send_sense_request() { static_cast<void>(get_blocking()); }

    void AngleManager::set_event_sink(std::function<void(const AngleData &)> sink) { relay_.set_sink(std::move(sink)); }

} // namespace linkerbot::hand::o30

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <linkerbot/hand/o30/angle.hpp>
#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/runtime/cache.hpp>

namespace linkerbot::hand::o30 {

    // MI 0x01 (8-bit position), 0x20 (16-bit full read), 0x30 (sparse write).
    class AngleManager {
        friend class O30;

    public:
        explicit AngleManager(Client &client);

        AngleManager(const AngleManager &) = delete;
        AngleManager &operator=(const AngleManager &) = delete;
        AngleManager(AngleManager &&) = delete;
        AngleManager &operator=(AngleManager &&) = delete;

        // Protocol-native bytes; no percentage or direction conversion.
        void set_angles(const std::array<uint8_t, kJointCount> &angles,
                        std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        // Percentages follow Angle's per-joint direction, including on readback.
        void set_angles_percent(const Angle &angles,
                                std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_angles_percent(const std::array<float, kJointCount> &percents,
                                std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        // All three groups below contain only flexion joints: 0 % extended,
        // 100 % flexed. The raw variants send their bytes unchanged.
        void set_tips(const std::array<float, kTipCount> &percents,
                      std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_raw_tips(const std::array<uint8_t, kTipCount> &raw,
                          std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_roots1(const std::array<float, kRoot1Count> &percents,
                        std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_raw_roots1(const std::array<uint8_t, kRoot1Count> &raw,
                            std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_roots2(const std::array<float, kRoot2Count> &percents,
                        std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});
        void set_raw_roots2(const std::array<uint8_t, kRoot2Count> &raw,
                            std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        // (Legacy finger-major sparse id, raw value) pairs. On HOP 0.0.4,
        // emulated with MI 0x01 single-slot writes: MI 0x30 is incompatible.
        // Only mounted IDs are accepted on either revision. Multiple writes are not atomic.
        void set_sparse(const std::vector<std::pair<uint8_t, uint8_t>> &slot_values,
                        std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        // Same as set_sparse, but the first of each pair is a 0..19 joint index.
        void set_sparse_joints(const std::vector<std::pair<size_t, uint8_t>> &joint_values,
                               std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100});

        [[nodiscard]] AngleData get_blocking(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                                             ValueKind kind = ValueKind::kActual);

        [[nodiscard]] std::array<uint8_t, kSlotCount>
        get_slots(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{100},
                  ValueKind kind = ValueKind::kActual);

        // Full 72-byte read of MI 0x20. Local SI offsets currently fail.
        [[nodiscard]] HalfwordPositionData
        get_halfword(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds{200});

        [[nodiscard]] std::optional<AngleData> get_snapshot() const;

    private:
        void send_sense_request();
        void set_event_sink(std::function<void(const AngleData &)> sink);

        Client *client_;
        runtime::Cache<AngleData> relay_;
    };

} // namespace linkerbot::hand::o30

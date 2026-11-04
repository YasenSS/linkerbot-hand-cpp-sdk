#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace linkerbot::hand::l30 {

    // L30 has 17 degrees of freedom. Every batch payload is ordered J1..J17.
    inline constexpr size_t kJointCount = 17;

    // Documented raw command range for one joint (protocol v6 section 6).
    struct JointSpec {
        std::string_view name;
        int16_t minimum;
        int16_t maximum;
        bool reverse_percent = false;
    };

    // Left-hand default ranges. The device clips out-of-limit positions
    // according to the configured hand type, so these are the command envelope
    // rather than a guarantee about the achieved position.
    inline constexpr std::array<JointSpec, kJointCount> kJointSpecs{{
        {"thumb_root", 0, 900, true},
        {"thumb_tip", 0, 1200, true},
        {"thumb_abd", 0, 900},
        {"thumb_rotation", 0, 800},
        {"ring_abd", -200, 200},
        {"ring_tip", 0, 1500, true},
        {"ring_root", 0, 1600, true},
        {"middle_root", 0, 1600, true},
        {"middle_tip", 0, 1500, true},
        {"pinky_root", 0, 1600, true},
        {"pinky_tip", 0, 1500, true},
        {"pinky_abd", -200, 200},
        {"middle_abd", -200, 200},
        {"index_abd", -200, 200},
        {"index_root", 0, 1600, true},
        {"index_tip", 0, 1500, true},
        {"wrist", -1000, 1000},
    }};

    // Current limit: uint16, 60..800 counts, 6.5 mA per count (section 7.1).
    // Speed:  uint16, 1..250 counts, 0.732 rpm per count (section 7.1).
    //
    // The scale factors are exposed for callers that want engineering units,
    // but the API itself deals in raw counts throughout — same as the Python
    // SDK, which returns what the device reports without converting.
    inline constexpr uint16_t kCurrentLimitMin = 60;
    inline constexpr uint16_t kCurrentLimitMax = 800;
    inline constexpr float kCurrentMilliampsPerCount = 6.5F;

    inline constexpr uint16_t kSpeedMin = 1;
    inline constexpr uint16_t kSpeedMax = 250;
    inline constexpr float kSpeedRpmPerCount = 0.732F;

    [[nodiscard]] constexpr std::string_view joint_name(size_t index) {
        if (index >= kJointSpecs.size()) {
            throw std::out_of_range("joint index out of range");
        }
        return kJointSpecs.at(index).name;
    }

    // Flexion: 0 % -> maximum, 100 % -> minimum, matching Python's public API.
    // Other axes: 0 % -> minimum, 100 % -> maximum; symmetric axes center at 50 %.
    [[nodiscard]] int16_t percent_to_raw(float percent, const JointSpec &spec);
    [[nodiscard]] float raw_to_percent(int16_t raw, const JointSpec &spec);

    [[nodiscard]] std::array<int16_t, kJointCount> percents_to_raw(const std::array<float, kJointCount> &percents);
    [[nodiscard]] std::array<float, kJointCount> raw_to_percents(const std::array<int16_t, kJointCount> &raw);

    // Throws std::invalid_argument when any entry falls outside [0, 100].
    void validate_percents(const std::array<float, kJointCount> &percents, std::string_view label);

    // Throws std::invalid_argument when any entry falls outside its joint spec.
    void validate_raw_commands(const std::array<int16_t, kJointCount> &raw, std::string_view label);

    // Throws std::invalid_argument when any entry falls outside [minimum, maximum].
    void validate_u16_range(const std::array<uint16_t, kJointCount> &values, uint16_t minimum, uint16_t maximum,
                            std::string_view label);

} // namespace linkerbot::hand::l30

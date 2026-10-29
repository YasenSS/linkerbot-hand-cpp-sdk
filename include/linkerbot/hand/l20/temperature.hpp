#pragma once

#include <array>
#include <chrono>

#include <linkerbot/hand/l20/joints.hpp>

namespace linkerbot::hand::l20 {

    struct Temperature {
        static constexpr size_t kJointCount = l20::kJointCount;

        float thumb_abd = 0.0f;
        float thumb_yaw = 0.0f;
        float thumb_root1 = 0.0f;
        float thumb_tip = 0.0f;
        float index_abd = 0.0f;
        float index_root1 = 0.0f;
        float index_tip = 0.0f;
        float middle_abd = 0.0f;
        float middle_root1 = 0.0f;
        float middle_tip = 0.0f;
        float ring_abd = 0.0f;
        float ring_root1 = 0.0f;
        float ring_tip = 0.0f;
        float pinky_abd = 0.0f;
        float pinky_root1 = 0.0f;
        float pinky_tip = 0.0f;

        Temperature() = default;
        Temperature(float t_abd, float t_yaw, float t_root, float t_tip, float i_abd, float i_root, float i_tip,
                    float m_abd, float m_root, float m_tip, float r_abd, float r_root, float r_tip, float p_abd,
                    float p_root, float p_tip)
            : thumb_abd(t_abd), thumb_yaw(t_yaw), thumb_root1(t_root), thumb_tip(t_tip), index_abd(i_abd),
              index_root1(i_root), index_tip(i_tip), middle_abd(m_abd), middle_root1(m_root), middle_tip(m_tip),
              ring_abd(r_abd), ring_root1(r_root), ring_tip(r_tip), pinky_abd(p_abd), pinky_root1(p_root),
              pinky_tip(p_tip) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_abd,    thumb_yaw,  thumb_root1, thumb_tip,  index_abd, index_root1, index_tip,   middle_abd,
                    middle_root1, middle_tip, ring_abd,    ring_root1, ring_tip,  pinky_abd,   pinky_root1, pinky_tip};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        [[nodiscard]] static Temperature from_array(const std::array<float, kJointCount> &values) {
            return {values[0], values[1], values[2],  values[3],  values[4],  values[5],  values[6],  values[7],
                    values[8], values[9], values[10], values[11], values[12], values[13], values[14], values[15]};
        }
    };

    struct TemperatureData {
        Temperature temperature;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l20

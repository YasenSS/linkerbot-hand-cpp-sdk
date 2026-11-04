#pragma once

#include <array>
#include <chrono>

#include <linkerbot/hand/l20/joints.hpp>

namespace linkerbot::hand::l20 {

    struct Speed {
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

        Speed() = default;
        Speed(float t_abd, float t_yaw, float t_root, float t_tip, float i_abd, float i_root, float i_tip, float m_abd,
              float m_root, float m_tip, float r_abd, float r_root, float r_tip, float p_abd, float p_root, float p_tip)
            : Speed(from_fields({t_abd, t_yaw, t_root, t_tip, i_abd, i_root, i_tip, m_abd, m_root, m_tip, r_abd, r_root,
                                 r_tip, p_abd, p_root, p_tip})) {}

        [[nodiscard]] std::array<float, kJointCount> to_array() const {
            return {thumb_abd,    thumb_yaw,  thumb_root1, thumb_tip,  index_abd, index_root1, index_tip,   middle_abd,
                    middle_root1, middle_tip, ring_abd,    ring_root1, ring_tip,  pinky_abd,   pinky_root1, pinky_tip};
        }

        [[nodiscard]] float operator[](size_t idx) const { return to_array().at(idx); }

        void validate() const { validate_percents(to_array(), "Speed"); }

        [[nodiscard]] static Speed from_array(const std::array<float, kJointCount> &values) {
            validate_percents(values, "Speed");

            return from_fields(values);
        }

    private:
        static Speed from_fields(const std::array<float, kJointCount> &values) {
            Speed result;
            result.thumb_abd = values[0];
            result.thumb_yaw = values[1];
            result.thumb_root1 = values[2];
            result.thumb_tip = values[3];
            result.index_abd = values[4];
            result.index_root1 = values[5];
            result.index_tip = values[6];
            result.middle_abd = values[7];
            result.middle_root1 = values[8];
            result.middle_tip = values[9];
            result.ring_abd = values[10];
            result.ring_root1 = values[11];
            result.ring_tip = values[12];
            result.pinky_abd = values[13];
            result.pinky_root1 = values[14];
            result.pinky_tip = values[15];
            result.validate();
            return result;
        }
    };

    struct SpeedData {
        Speed speed;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l20

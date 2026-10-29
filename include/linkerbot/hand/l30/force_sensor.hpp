#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <linkerbot/hand/l30/protocol.hpp>

namespace linkerbot::hand::l30 {

    enum class Finger { kThumb, kIndex, kMiddle, kRing, kPinky };

    inline constexpr std::array<Finger, 5> kAllFingers{Finger::kThumb, Finger::kIndex, Finger::kMiddle, Finger::kRing,
                                                       Finger::kPinky};

    [[nodiscard]] std::string_view finger_name(Finger finger);

    // Tactile parent (0x2) subcommand for one finger.
    [[nodiscard]] uint8_t finger_subcommand(Finger finger);

    // One finger's 12x6 tactile matrix, reassembled from the 72-byte payload the
    // two response frames carry.
    struct FingerForceData {
        static constexpr size_t kRows = protocol::kTactileRows;
        static constexpr size_t kColumns = protocol::kTactileColumns;

        Finger finger = Finger::kThumb;
        std::array<std::array<uint8_t, kColumns>, kRows> values{};
        std::chrono::steady_clock::time_point timestamp;

        [[nodiscard]] static FingerForceData
        from_payload(Finger finger, const std::array<uint8_t, protocol::kTactileDataLength> &payload,
                     std::chrono::steady_clock::time_point timestamp);
    };

    struct AllFingersData {
        FingerForceData thumb;
        FingerForceData index;
        FingerForceData middle;
        FingerForceData ring;
        FingerForceData pinky;
        std::chrono::steady_clock::time_point timestamp;
    };

} // namespace linkerbot::hand::l30

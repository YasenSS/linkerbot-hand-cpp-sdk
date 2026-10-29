#pragma once

#include <linkerbot/hand/fault_status.hpp>

#include <array>
#include <chrono>
#include <cstdint>

#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    // HOP 0.0.4 fault bits (MI 0x0C). On 0.0.3, faults come from MI 0x0D;
    // raw bytes remain available, but the bit meanings are not established by
    // the 0.0.3 measured protocol. Treat any nonzero legacy byte as a fault.
    // Not confirmed by reproducing faults on hardware. Bits 4/5 are reserved.
    enum class FaultCode : uint8_t {
        kHandLayerStall = 0x01,
        kActuatorLayerStall = 0x02,
        kOverCurrent = 0x04,
        kOverTemperature = 0x08,
        kActuatorAbnormal = 0x40,
        kActuatorOffline = 0x80,
    };

    struct FaultData {
        std::array<uint8_t, kJointCount> faults{};
        ValueKind kind = ValueKind::kActual;
        std::chrono::steady_clock::time_point timestamp;
        // Keep standalone decoding compatible with the documented 0.0.4 bits.
        // FaultManager always records the revision read from the device.
        protocol::Revision revision = protocol::Revision::kV004;

        [[nodiscard]] bool has_any_fault(size_t joint_index) const {
            const auto decoded = status(joint_index);
            return revision == protocol::Revision::kV003 ? decoded.has_any_bit() : decoded.has_any_fault();
        }

        [[nodiscard]] FaultStatus status(size_t joint_index) const {
            return decode_fault_status(faults.at(joint_index), revision == protocol::Revision::kV004 ? 0xCF : 0);
        }

        [[nodiscard]] bool has_fault(size_t joint_index, FaultCode code) const {
            return (status(joint_index).known_bits & static_cast<uint8_t>(code)) != 0;
        }
    };

} // namespace linkerbot::hand::o30

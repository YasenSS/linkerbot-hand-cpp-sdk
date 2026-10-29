#include "linkerbot/hand/o30/service_manager.hpp"
#include <linkerbot/hand/errors.hpp>

#include <stdexcept>

#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    namespace {
        constexpr uint8_t kSiPassword = 0x00;
        constexpr uint8_t kSiCalibFlags = 0x1E;
        constexpr uint8_t kSiCalibCount = 0x23;
        constexpr uint8_t kSiCalibDate = 0x25;
        constexpr uint8_t kSiCommFault = 0x3D;
        constexpr uint8_t kSiHeartbeat = 0x45;
        constexpr uint8_t kSiHandSide = 0x49;
        constexpr uint8_t kSiSensorType = 0x4A;
        constexpr uint8_t kSiPowerOn = 0x4B;
        constexpr uint8_t kSiTotalRuntime = 0x5B;
        constexpr uint8_t kSiMotorRuntime = 0x6B;
        constexpr uint8_t kSiTaskFrequency = 0x7B;
        constexpr uint8_t kSiDeviceUid = 0xAB;
        constexpr uint8_t kSiProtocolIf = 0xCD;

        template <size_t N> std::array<uint8_t, N> copy_prefix(const std::vector<uint8_t> &payload) {
            std::array<uint8_t, N> out{};
            const auto n = payload.size() < N ? payload.size() : N;
            for (size_t i = 0; i < n; ++i) {
                out[i] = payload[i];
            }
            return out;
        }
    } // namespace

    ServiceManager::ServiceManager(Client &client) : client_(&client) {}

    std::vector<uint8_t> ServiceManager::read_field(uint8_t si, uint8_t edl, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        auto payload = client_->read(protocol::kMiService, si, edl, false, timeout_ms);
        if (payload.size() != edl) {
            throw protocol::ProtocolError("short engineering service field at SI " + protocol::hex_byte(si));
        }
        return payload;
    }

    void ServiceManager::write_field(uint8_t si, const std::vector<uint8_t> &payload,
                                     std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        protocol::require_positive_timeout(timeout_ms);
        static_cast<void>(client_->write(protocol::kMiService, si, payload, timeout_ms));
    }

    ServiceStatus ServiceManager::get_status(std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        ServiceStatus status{};
        status.password = copy_prefix<8>(read_field(kSiPassword, 8, timeout_ms));
        status.calibration_flags = copy_prefix<5>(read_field(kSiCalibFlags, 5, timeout_ms));
        const auto count = read_field(kSiCalibCount, 2, timeout_ms);
        if (count.size() >= 2) {
            status.calibration_count = protocol::read_u16_le(count.data());
        }
        status.calibration_date = protocol::decode_ascii(read_field(kSiCalibDate, 0x18, timeout_ms));
        status.comm_fault_flags = copy_prefix<8>(read_field(kSiCommFault, 8, timeout_ms));
        const auto heartbeat = read_field(kSiHeartbeat, 4, timeout_ms);
        if (heartbeat.size() >= 4) {
            status.heartbeat = protocol::read_u32_le(heartbeat.data());
        }
        const auto side = read_field(kSiHandSide, 1, timeout_ms);
        if (!side.empty()) {
            status.hand_side = side[0];
        }
        const auto sensor = read_field(kSiSensorType, 1, timeout_ms);
        if (!sensor.empty()) {
            status.sensor_type = sensor[0];
        }
        status.power_on_time = protocol::decode_ascii(read_field(kSiPowerOn, 0x10, timeout_ms));
        status.total_runtime = protocol::decode_ascii(read_field(kSiTotalRuntime, 0x10, timeout_ms));
        status.motor_runtime = protocol::decode_ascii(read_field(kSiMotorRuntime, 0x10, timeout_ms));
        status.task_frequency = read_field(kSiTaskFrequency, 0x30, timeout_ms);
        status.device_uid = protocol::decode_ascii(read_field(kSiDeviceUid, 0x20, timeout_ms));
        const auto iface = read_field(kSiProtocolIf, 1, timeout_ms);
        if (!iface.empty()) {
            status.protocol_interface = iface[0];
        }
        status.timestamp = client_->received_at();
        return status;
    }

    void ServiceManager::set_hand_side(uint8_t side, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_field(kSiHandSide, {side}, timeout_ms);
    }

    void ServiceManager::set_sensor_type(uint8_t type, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_field(kSiSensorType, {type}, timeout_ms);
    }

    void ServiceManager::set_protocol_interface(uint8_t value, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_field(kSiProtocolIf, {value}, timeout_ms);
    }

    void ServiceManager::set_password(const std::array<uint8_t, 8> &password, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        write_field(kSiPassword, std::vector<uint8_t>(password.begin(), password.end()), timeout_ms);
    }

    void ServiceManager::set_device_uid(const std::vector<uint8_t> &uid, std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        if (uid.size() != 0x20) {
            throw ValidationError("device UID must be 32 bytes");
        }
        write_field(kSiDeviceUid, uid, timeout_ms);
    }

} // namespace linkerbot::hand::o30

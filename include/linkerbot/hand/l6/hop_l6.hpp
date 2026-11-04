#pragma once
#include <array>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/api/config.hpp>
#include <linkerbot/hand/api/events.hpp>
#include <linkerbot/hand/api/snapshot.hpp>
#include <linkerbot/hand/l6/hop_protocol.hpp>
#include <linkerbot/hand/l6/side.hpp>
#include <linkerbot/hand/runtime/call_drain.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <memory>
#include <mutex>
#include <string_view>

namespace linkerbot::hand::l6 {
    // HOP v0.0.4 has different fault meanings from classic L6 FaultCode.
    enum class HopFaultCode : uint8_t {
        kHandStall = 0x01,
        kActuatorStall = 0x02,
        kOvercurrent = 0x04,
        kOvertemperature = 0x08,
        kActuatorAbnormal = 0x40,
        kActuatorOffline = 0x80
    };

    // Latest L6 firmware: standard CAN FD HOP, no BRS. Position counts are u16;
    // other numeric channels remain raw u8 counts with unverified physical scales.
    // All managers share one runtime; stop() leaves a borrowed Session running.
    class HopL6 {
        std::shared_ptr<transport::Session> session_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        bool owns_session_ = false;
        uint32_t request_id_;
        DeviceCapabilities capabilities_;
        runtime::CallDrain calls_;

    public:
        class AngleManager {
        public:
            AngleManager(const AngleManager &) = delete;
            AngleManager &operator=(const AngleManager &) = delete;
            void set_angles(const std::array<uint16_t, 6> &values,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
            void set_angles_percent(const std::array<float, 6> &values,
                                    std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
            [[nodiscard]] AngleSample get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
            [[nodiscard]] std::optional<AngleSample> get_snapshot() const;

        private:
            friend class HopL6;
            explicit AngleManager(HopL6 &owner) : owner_(&owner) {}
            HopL6 *owner_;
        };
        class VectorManager {
        public:
            VectorManager(const VectorManager &) = delete;
            VectorManager &operator=(const VectorManager &) = delete;
            void set_values(const std::array<uint8_t, 6> &values,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
            [[nodiscard]] RawVectorSample get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                                           100});
            [[nodiscard]] std::optional<RawVectorSample> get_snapshot() const;

        private:
            friend class HopL6;
            struct VectorBinding {
                uint8_t mi;
                bool configured;
                const char *domain;
                const char *family;
            };
            VectorManager(HopL6 &owner, uint8_t mi, bool configured, const char *domain, const char *family = nullptr)
                : VectorManager(owner, VectorBinding{mi, configured, domain, family}) {}
            VectorManager(HopL6 &owner, const VectorBinding &binding)
                : owner_(&owner), mi_(binding.mi), configured_(binding.configured), domain_(binding.domain),
                  family_(binding.family) {}
            HopL6 *owner_;
            uint8_t mi_;
            bool configured_;
            const char *domain_;
            const char *family_;
        };
        class FaultManager {
        public:
            FaultManager(const FaultManager &) = delete;
            FaultManager &operator=(const FaultManager &) = delete;
            [[nodiscard]] FaultSample get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
            [[nodiscard]] std::optional<FaultSample> get_snapshot() const;

        private:
            friend class HopL6;
            explicit FaultManager(HopL6 &owner) : owner_(&owner) {}
            HopL6 *owner_;
        };
        class ForceSensorManager {
        public:
            ForceSensorManager(const ForceSensorManager &) = delete;
            ForceSensorManager &operator=(const ForceSensorManager &) = delete;
            [[nodiscard]] ForceSample get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});
            [[nodiscard]] std::optional<ForceSample> get_snapshot() const;

        private:
            friend class HopL6;
            explicit ForceSensorManager(HopL6 &owner) : owner_(&owner) {}
            HopL6 *owner_;
        };
        class VersionManager {
        public:
            VersionManager(const VersionManager &) = delete;
            VersionManager &operator=(const VersionManager &) = delete;
            [[nodiscard]] DeviceIdentity get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                                          1000});
            [[nodiscard]] std::optional<DeviceIdentity> get_snapshot() const;

        private:
            friend class HopL6;
            explicit VersionManager(HopL6 &owner) : owner_(&owner) {}
            HopL6 *owner_;
        };
        class ConfigManager {
        public:
            ConfigManager(const ConfigManager &) = delete;
            ConfigManager &operator=(const ConfigManager &) = delete;
            void save(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});

        private:
            friend class HopL6;
            explicit ConfigManager(HopL6 &owner) : owner_(&owner) {}
            HopL6 *owner_;
        };

        HopL6(Side side, std::string_view interface_name, uint32_t request_id = hop::kDefaultRequestId);
        HopL6(Side side, std::unique_ptr<can::CANTransport> transport, uint32_t request_id = hop::kDefaultRequestId);
        HopL6(Side side, std::shared_ptr<transport::Session> session, uint32_t request_id = hop::kDefaultRequestId);
        ~HopL6();
        HopL6(const HopL6 &) = delete;
        HopL6 &operator=(const HopL6 &) = delete;
        HopL6(HopL6 &&) = delete;
        HopL6 &operator=(HopL6 &&) = delete;

        [[nodiscard]] uint32_t request_id() const { return request_id_; }
        [[nodiscard]] uint32_t response_id() const { return hop::response_id(request_id_); }
        [[nodiscard]] std::shared_ptr<runtime::DeviceRuntime> runtime() const { return runtime_; }
        [[nodiscard]] const DeviceCapabilities &capabilities() const noexcept { return capabilities_; }
        [[nodiscard]] HandHealth health() const { return runtime_->health(); }
        [[nodiscard]] HandSnapshot get_snapshot() const;
        void start_polling(const PollingPlan &plan = {});
        void stop_polling();
        [[nodiscard]] std::shared_ptr<EventQueue<HandEvent>> stream_handle(size_t max_size = 100);
        EventQueue<HandEvent> &stream(size_t max_size = 100) { return *stream_handle(max_size); }
        void stop_stream();
        void recover();
        void stop();

        AngleManager angle;
        VectorManager speed, current_limit, current, temperature;
        FaultManager fault;
        ForceSensorManager force_sensor;
        VersionManager version;
        ConfigManager config;

    private:
        runtime::DeviceRuntime::Operation operation(std::chrono::milliseconds timeout);
        std::vector<uint8_t> read_range(uint8_t mi, uint8_t si, size_t length, bool rts, runtime::Deadline deadline);
        runtime::Sample execute(runtime::TransactionPlan plan, runtime::Deadline deadline);
        SampleMeta metadata(const runtime::Sample &provenance, const char *domain, bool configured = false) const;
        void publish(std::string domain, HandEvent event, runtime::Sample provenance);
        mutable std::mutex event_mutex_;
        std::mutex lifecycle_mutex_;
        std::shared_ptr<EventQueue<HandEvent>> events_;
        std::vector<std::shared_ptr<EventQueue<HandEvent>>> retired_streams_;
    };
}

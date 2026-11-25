#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/event_queue.hpp>
#include <linkerbot/hand/api/can_configuration.hpp>
#include <linkerbot/hand/api/config.hpp>
#include <linkerbot/hand/api/events.hpp>
#include <linkerbot/hand/api/snapshot.hpp>
#include <linkerbot/hand/o6/hop_protocol.hpp>
#include <linkerbot/hand/o6/side.hpp>
#include <linkerbot/hand/runtime/call_drain.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace linkerbot::hand::o6 {
    // HOP v0.0.4 has different fault meanings from classic O6 FaultCode.
    enum class HopFaultCode : uint8_t {
        kHandStall = 0x01,
        kActuatorStall = 0x02,
        kOvercurrent = 0x04,
        kOvertemperature = 0x08,
        kActuatorAbnormal = 0x40,
        kActuatorOffline = 0x80
    };

    // O6 HOP v0.0.4 firmware: standard CAN FD (no BRS) or Classic CAN 2.0.
    // All 0x01..0x0C channels use single-byte u8 counts (0..255); MI 0x20 is
    // unsupported by O6 HOP v0.0.4. All managers share one runtime; stop()
    // leaves a borrowed Session running.
    class HopO6 {
        std::shared_ptr<transport::Session> session_;
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        bool owns_session_ = false;
        uint32_t request_id_;
        bool can_fd_ = true;
        DeviceCapabilities capabilities_;
        runtime::CallDrain calls_;

    public:
        class AngleManager {
        public:
            AngleManager(const AngleManager &) = delete;
            AngleManager &operator=(const AngleManager &) = delete;
            void set_angles(const std::array<uint8_t, 6> &values,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
            void set_angles_percent(const std::array<float, 6> &values,
                                    std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
            [[nodiscard]] AngleSample get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
            [[nodiscard]] std::optional<AngleSample> get_snapshot() const;

        private:
            friend class HopO6;
            explicit AngleManager(HopO6 &owner) : owner_(&owner) {}
            HopO6 *owner_;
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
            friend class HopO6;
            struct VectorBinding {
                uint8_t mi;
                bool configured;
                TelemetrySemantic semantic;
                const char *domain;
                const char *family;
            };
            VectorManager(HopO6 &owner, uint8_t mi, bool configured, TelemetrySemantic semantic, const char *domain,
                          const char *family = nullptr)
                : VectorManager(owner, VectorBinding{mi, configured, semantic, domain, family}) {}
            VectorManager(HopO6 &owner, const VectorBinding &binding)
                : owner_(&owner), mi_(binding.mi), configured_(binding.configured), semantic_(binding.semantic),
                  domain_(binding.domain), family_(binding.family) {}
            HopO6 *owner_;
            uint8_t mi_;
            bool configured_;
            TelemetrySemantic semantic_;
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
            friend class HopO6;
            explicit FaultManager(HopO6 &owner) : owner_(&owner) {}
            HopO6 *owner_;
        };

        class ForceSensorManager {
        public:
            ForceSensorManager(const ForceSensorManager &) = delete;
            ForceSensorManager &operator=(const ForceSensorManager &) = delete;
            [[nodiscard]] ForceSample get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});
            [[nodiscard]] std::optional<ForceSample> get_snapshot() const;

        private:
            friend class HopO6;
            explicit ForceSensorManager(HopO6 &owner) : owner_(&owner) {}
            HopO6 *owner_;
        };

        class VersionManager {
        public:
            VersionManager(const VersionManager &) = delete;
            VersionManager &operator=(const VersionManager &) = delete;
            [[nodiscard]] DeviceIdentity get_blocking(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                                          1000});
            [[nodiscard]] std::optional<DeviceIdentity> get_snapshot() const;

        private:
            friend class HopO6;
            explicit VersionManager(HopO6 &owner) : owner_(&owner) {}
            HopO6 *owner_;
        };

        class ConfigManager {
        public:
            ConfigManager(const ConfigManager &) = delete;
            ConfigManager &operator=(const ConfigManager &) = delete;
            void save(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
            [[nodiscard]] HopCanConfiguration
            get_can_configuration(std::chrono::milliseconds timeout = std::chrono::milliseconds{200});
            [[nodiscard]] HopCanModeSwitchResult
            set_can_interface_mode(HopCanInterfaceMode mode, bool save_to_flash = false,
                                   std::chrono::milliseconds timeout = std::chrono::milliseconds{500});

        private:
            friend class HopO6;
            explicit ConfigManager(HopO6 &owner) : owner_(&owner) {}
            HopO6 *owner_;
        };

        HopO6(Side side, std::string_view interface_name, uint32_t request_id = hop::kDefaultRequestId,
              bool can_fd = true);
        HopO6(Side side, std::unique_ptr<can::CANTransport> transport, uint32_t request_id = hop::kDefaultRequestId,
              bool can_fd = true);
        HopO6(Side side, std::shared_ptr<transport::Session> session, uint32_t request_id = hop::kDefaultRequestId,
              bool can_fd = true);
        ~HopO6();
        HopO6(const HopO6 &) = delete;
        HopO6 &operator=(const HopO6 &) = delete;
        HopO6(HopO6 &&) = delete;
        HopO6 &operator=(HopO6 &&) = delete;

        [[nodiscard]] uint32_t request_id() const { return request_id_; }
        [[nodiscard]] uint32_t response_id() const { return hop::response_id(request_id_); }
        [[nodiscard]] bool can_fd() const noexcept { return can_fd_; }
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
        VectorManager speed, acceleration, current_limit, current, temperature;
        VectorManager stall_detection_time, stall_threshold, stall_hold_current;
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
} // namespace linkerbot::hand::o6

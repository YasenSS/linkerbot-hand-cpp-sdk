#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <linkerbot/can/pcan_transport.hpp>
#include <linkerbot/hand/api/device.hpp>

namespace linkerbot::hand {
    enum class DiscoveryStatus : uint8_t { kVerified, kPartial };

    struct PCANDiscoveryConfig {
        std::vector<std::string> channels;
        std::vector<TransportKind> probe_protocols{TransportKind::kCan};
        uint32_t nominal_bitrate = 1'000'000U;
        uint32_t data_bitrate = 5'000'000U;
        std::optional<std::filesystem::path> library_path;
        can::PcanFdTiming fd_timing;
    };

    enum class SLCANProtocol { kLawicelClassic, kCanableFd };

    struct SLCANDiscoveryConfig {
        // Explicit user allowlist. Empty means no serial access, including
        // when common DiscoveryConfig.channels contains COM or /dev/... names.
        std::vector<std::string> channels;
        std::vector<TransportKind> probe_protocols{TransportKind::kCan};
        uint32_t nominal_bitrate = 1'000'000U;
        uint32_t data_bitrate = 5'000'000U;
        uint32_t serial_baud = 115200;
        std::chrono::milliseconds command_timeout{500};
        SLCANProtocol protocol = SLCANProtocol::kLawicelClassic;
        std::chrono::milliseconds startup_delay{0};
    };

    struct MiniCANFDChannel {
        uint32_t device_index = 0;
        uint32_t channel_index = 0;
    };

    struct MiniCANFDDiscoveryConfig {
        std::optional<std::filesystem::path> library_path;
        std::vector<MiniCANFDChannel> channels;
        uint32_t nominal_bitrate = 1'000'000U;
        uint32_t data_bitrate = 5'000'000U;
    };

    struct DiscoveryConfig {
        DiscoveryConfig();

        // An empty list enables every backend offered by the selected source.
        std::vector<std::string> backends;
        // An empty list means every channel offered by the selected source.
        // Values may be a unified channel name or a backend-native name.
        std::vector<std::string> channels;
        std::chrono::milliseconds response_timeout{120};
        std::chrono::milliseconds send_timeout{100};
        std::chrono::milliseconds inter_request_delay{2};
        std::chrono::milliseconds total_timeout{30000};
        size_t route_queue_capacity = 256;
        std::vector<uint8_t> l30_node_ids;
        std::vector<uint32_t> o30_request_ids{0x001U, 0x002U};

        // Backend-specific settings stay isolated from the common scan policy.
        PCANDiscoveryConfig pcan;
        SLCANDiscoveryConfig slcan;
        MiniCANFDDiscoveryConfig minicanfd;
    };

    class DiscoveryChannel {
    public:
        DiscoveryChannel(std::string provider, std::string name, TransportKind kind,
                         std::optional<uint32_t> nominal_bitrate = {}, std::optional<uint32_t> data_bitrate = {},
                         bool supports_classic = true, std::optional<bool> supports_fd = {},
                         std::string configuration_id = {});
        DiscoveryChannel(std::string backend, std::string name, std::string native_name, TransportKind kind,
                         std::optional<uint32_t> nominal_bitrate = {}, std::optional<uint32_t> data_bitrate = {},
                         bool supports_classic = true, std::optional<bool> supports_fd = {},
                         std::string configuration_id = {});

        // provider() is retained as a compatibility alias for backend().
        [[nodiscard]] const std::string &provider() const noexcept;
        [[nodiscard]] const std::string &backend() const noexcept;
        // Stable cross-backend name exposed to discovery callers.
        [[nodiscard]] const std::string &name() const noexcept;
        // Name understood by the backend/driver when opening the channel.
        [[nodiscard]] const std::string &native_name() const noexcept;
        [[nodiscard]] TransportKind kind() const noexcept;
        [[nodiscard]] std::optional<uint32_t> nominal_bitrate() const noexcept;
        [[nodiscard]] std::optional<uint32_t> data_bitrate() const noexcept;
        [[nodiscard]] bool supports_classic() const noexcept;
        [[nodiscard]] std::optional<bool> supports_fd() const noexcept;
        [[nodiscard]] const std::string &configuration_id() const noexcept;

    private:
        std::string provider_;
        std::string name_;
        std::string native_name_;
        TransportKind kind_ = TransportKind::kCan;
        std::optional<uint32_t> nominal_bitrate_;
        std::optional<uint32_t> data_bitrate_;
        bool supports_classic_ = true;
        std::optional<bool> supports_fd_;
        std::string configuration_id_;
    };

    class DiscoveryAddress {
    public:
        DiscoveryAddress(std::string scheme, uint32_t request_id, uint32_t response_id, bool extended_id,
                         std::optional<uint8_t> node_id = {}, std::optional<uint8_t> host_id = {});

        [[nodiscard]] const std::string &scheme() const noexcept;
        [[nodiscard]] uint32_t request_id() const noexcept;
        [[nodiscard]] uint32_t response_id() const noexcept;
        [[nodiscard]] bool extended_id() const noexcept;
        [[nodiscard]] std::optional<uint8_t> node_id() const noexcept;
        [[nodiscard]] std::optional<uint8_t> host_id() const noexcept;

    private:
        std::string scheme_;
        uint32_t request_id_ = 0;
        uint32_t response_id_ = 0;
        bool extended_id_ = false;
        std::optional<uint8_t> node_id_;
        std::optional<uint8_t> host_id_;
    };

    class DiscoveryProblem {
    public:
        DiscoveryProblem(std::string code, std::string message);

        [[nodiscard]] const std::string &code() const noexcept;
        [[nodiscard]] const std::string &message() const noexcept;

    private:
        std::string code_;
        std::string message_;
    };

    class DiscoveryFailure {
    public:
        DiscoveryFailure(std::string operation, std::string code, std::string message,
                         std::optional<std::string> channel = {}, std::optional<std::string> backend = {},
                         std::optional<std::string> stage = {}, std::optional<std::string> native_channel = {},
                         std::optional<std::string> error_type = {}, std::optional<int> native_status = {});

        [[nodiscard]] const std::string &operation() const noexcept;
        [[nodiscard]] const std::string &code() const noexcept;
        [[nodiscard]] const std::string &message() const noexcept;
        [[nodiscard]] const std::optional<std::string> &channel() const noexcept;
        [[nodiscard]] const std::optional<std::string> &backend() const noexcept;
        [[nodiscard]] const std::optional<std::string> &stage() const noexcept;
        [[nodiscard]] const std::optional<std::string> &native_channel() const noexcept;
        [[nodiscard]] const std::optional<std::string> &error_type() const noexcept;
        [[nodiscard]] std::optional<int> native_status() const noexcept;

    private:
        std::string operation_;
        std::string code_;
        std::string message_;
        std::optional<std::string> channel_;
        std::optional<std::string> backend_;
        std::optional<std::string> stage_;
        std::optional<std::string> native_channel_;
        std::optional<std::string> error_type_;
        std::optional<int> native_status_;
    };

    class DiscoveryEnumeration {
    public:
        DiscoveryEnumeration(std::vector<DiscoveryChannel> channels = {}, std::vector<DiscoveryFailure> failures = {});

        [[nodiscard]] const std::vector<DiscoveryChannel> &channels() const noexcept;
        [[nodiscard]] const std::vector<DiscoveryFailure> &failures() const noexcept;

    private:
        std::vector<DiscoveryChannel> channels_;
        std::vector<DiscoveryFailure> failures_;
    };

    struct DiscoveryConnectionKey {
        std::string backend;
        std::string native_channel;
        TransportKind kind = TransportKind::kCan;
        std::optional<uint32_t> nominal_bitrate;
        std::optional<uint32_t> data_bitrate;
        std::string configuration_id; // includes resolved library and backend timing

        friend bool operator==(const DiscoveryConnectionKey &left, const DiscoveryConnectionKey &right) noexcept {
            return left.backend == right.backend && left.native_channel == right.native_channel &&
                   left.kind == right.kind && left.nominal_bitrate == right.nominal_bitrate &&
                   left.data_bitrate == right.data_bitrate && left.configuration_id == right.configuration_id;
        }
    };

    class DiscoveryTransportSpec {
    public:
        DiscoveryTransportSpec(std::string provider, std::string channel, TransportKind kind,
                               HandDevice::TransportFactory factory, std::optional<uint32_t> nominal_bitrate = {},
                               std::optional<uint32_t> data_bitrate = {},
                               std::optional<std::string> native_channel = {}, std::string configuration_id = {});

        [[nodiscard]] const std::string &provider() const noexcept;
        [[nodiscard]] const std::string &channel() const noexcept;
        [[nodiscard]] const std::string &native_channel() const noexcept;
        [[nodiscard]] TransportKind kind() const noexcept;
        [[nodiscard]] std::optional<uint32_t> nominal_bitrate() const noexcept;
        [[nodiscard]] std::optional<uint32_t> data_bitrate() const noexcept;
        [[nodiscard]] HandDevice::TransportFactory factory() const;
        [[nodiscard]] const std::string &configuration_id() const noexcept;
        [[nodiscard]] DiscoveryConnectionKey connection_key() const;

    private:
        std::string provider_;
        std::string channel_;
        std::string native_channel_;
        TransportKind kind_ = TransportKind::kCan;
        HandDevice::TransportFactory factory_;
        std::optional<uint32_t> nominal_bitrate_;
        std::optional<uint32_t> data_bitrate_;
        std::string configuration_id_;
    };

    class DiscoveryCandidate {
    public:
        DiscoveryCandidate(DiscoveryChannel channel, TransportKind wire_protocol, DiscoveryAddress address,
                           DiscoveryStatus status, DeviceIdentity identity, DiscoveryTransportSpec transport,
                           std::optional<DeviceCapabilities> capabilities = {},
                           std::vector<DiscoveryProblem> problems = {});

        [[nodiscard]] const DiscoveryChannel &channel() const noexcept;
        [[nodiscard]] TransportKind wire_protocol() const noexcept;
        [[nodiscard]] const DiscoveryAddress &address() const noexcept;
        [[nodiscard]] DiscoveryStatus status() const noexcept;
        [[nodiscard]] const DeviceIdentity &identity() const noexcept;
        [[nodiscard]] Model model() const noexcept;
        [[nodiscard]] HandSide side() const noexcept;
        [[nodiscard]] const DiscoveryTransportSpec &transport() const noexcept;
        // Discovery-time metadata for inspection only. HandDevice::open()
        // rereads identity and constructs the ready generation's verified
        // capabilities instead of trusting this snapshot.
        [[nodiscard]] const DeviceCapabilities *capabilities() const noexcept;
        [[nodiscard]] const std::vector<DiscoveryProblem> &problems() const noexcept;

        [[nodiscard]] HandDeviceConfig make_config() const;
        [[nodiscard]] HandDeviceConfig make_config(const HandRuntimeConfig &runtime) const;
        [[nodiscard]] std::unique_ptr<HandDevice> create_hand_device() const;
        [[nodiscard]] std::unique_ptr<HandDevice> create_hand_device(const HandRuntimeConfig &runtime) const;
        // A partial candidate is recoverable only when every recorded problem
        // is missing_side. The explicit side becomes part of the pinned
        // identity and is verified against any side evidence returned by open().
        [[nodiscard]] std::unique_ptr<HandDevice> create_hand_device(const HandRuntimeConfig &runtime,
                                                                     HandSide side) const;

    private:
        [[nodiscard]] HandDeviceConfig make_config(const HandRuntimeConfig &runtime,
                                                   std::optional<HandSide> requested_side) const;

        DiscoveryChannel channel_;
        TransportKind wire_protocol_ = TransportKind::kCan;
        DiscoveryAddress address_;
        DiscoveryStatus status_ = DiscoveryStatus::kPartial;
        DeviceIdentity identity_;
        DiscoveryTransportSpec transport_;
        std::optional<DeviceCapabilities> capabilities_;
        std::vector<DiscoveryProblem> problems_;
    };

    class DiscoveryReport {
    public:
        DiscoveryReport(std::vector<DiscoveryChannel> channels = {}, std::vector<DiscoveryCandidate> candidates = {},
                        std::vector<DiscoveryFailure> failures = {}, std::chrono::steady_clock::duration elapsed = {});

        [[nodiscard]] const std::vector<DiscoveryChannel> &channels() const noexcept;
        [[nodiscard]] const std::vector<DiscoveryCandidate> &candidates() const noexcept;
        [[nodiscard]] const std::vector<DiscoveryFailure> &failures() const noexcept;
        [[nodiscard]] std::chrono::steady_clock::duration elapsed() const noexcept;

        [[nodiscard]] std::vector<std::reference_wrapper<const DiscoveryCandidate>> verified_candidates() const;
        [[nodiscard]] std::vector<std::reference_wrapper<const DiscoveryCandidate>> partial_candidates() const;
        [[nodiscard]] const DiscoveryCandidate &require_one(bool allow_partial = false) const;

    private:
        std::vector<DiscoveryChannel> channels_;
        std::vector<DiscoveryCandidate> candidates_;
        std::vector<DiscoveryFailure> failures_;
        std::chrono::steady_clock::duration elapsed_{};
    };

    // Injectable boundary for platform enumeration and identity-only probing.
    // Implementations must return with all temporary transports closed.
    // total_timeout covers enumeration and probing together; implementations
    // must stop starting new work when that budget is exhausted.
    class DiscoverySource {
    public:
        virtual ~DiscoverySource() = default;

        DiscoverySource() = default;
        DiscoverySource(const DiscoverySource &) = delete;
        DiscoverySource &operator=(const DiscoverySource &) = delete;

        [[nodiscard]] virtual DiscoveryReport scan(const DiscoveryConfig &config) = 0;
    };

    struct DiscoveryBackendSource {
        // More than one source may contribute the same backend. This permits
        // plugin/fallback sources while keeping backend filtering consistent.
        std::string backend;
        std::shared_ptr<DiscoverySource> source;
    };

    // Scans independent sources in declaration order under one shared timeout
    // budget. Backend failures are isolated; aggregated output is sorted by
    // stable backend/native-channel/address keys.
    class CompositeDiscoverySource final : public DiscoverySource {
    public:
        explicit CompositeDiscoverySource(std::vector<DiscoveryBackendSource> sources);

        [[nodiscard]] DiscoveryReport scan(const DiscoveryConfig &config) override;

    private:
        std::vector<DiscoveryBackendSource> sources_;
    };

    // Selects only the discovery backends implemented for the host platform.
    // Construction performs no enumeration, driver loading or device I/O.
    class PlatformDiscoverySource final : public DiscoverySource {
    public:
        PlatformDiscoverySource();

        [[nodiscard]] DiscoveryReport scan(const DiscoveryConfig &config) override;

    private:
        std::unique_ptr<CompositeDiscoverySource> composite_;
        std::string unavailable_reason_;
    };

    // Linux SocketCAN discovery. The injectable constructor is also the
    // deterministic test seam: callbacks must return already-described
    // channels and a fresh, closed-lifetime transport for each invocation.
    class SocketCANDiscoverySource final : public DiscoverySource {
    public:
        using ChannelEnumerator = std::function<std::vector<DiscoveryChannel>()>;
        using TransportFactory = std::function<std::unique_ptr<can::CANTransport>(const std::string &, TransportKind)>;

        SocketCANDiscoverySource();
        SocketCANDiscoverySource(ChannelEnumerator channel_enumerator, TransportFactory transport_factory);
        SocketCANDiscoverySource(std::string backend, ChannelEnumerator channel_enumerator,
                                 TransportFactory transport_factory);

        [[nodiscard]] DiscoveryReport scan(const DiscoveryConfig &config) override;

    private:
        std::string backend_;
        ChannelEnumerator channel_enumerator_;
        TransportFactory transport_factory_;
    };

    using AdapterChannelEnumerator = std::function<DiscoveryEnumeration(const DiscoveryConfig &)>;
    using AdapterTransportFactory =
        std::function<std::unique_ptr<can::CANTransport>(const std::string &, TransportKind)>;

    class PCANDiscoverySource final : public DiscoverySource {
    public:
        PCANDiscoverySource();
        PCANDiscoverySource(AdapterChannelEnumerator channel_enumerator, AdapterTransportFactory transport_factory);

        [[nodiscard]] DiscoveryReport scan(const DiscoveryConfig &config) override;

    private:
        AdapterChannelEnumerator channel_enumerator_;
        AdapterTransportFactory transport_factory_;
        bool use_default_transport_ = false;
    };

    class SLCANDiscoverySource final : public DiscoverySource {
    public:
        SLCANDiscoverySource();
        SLCANDiscoverySource(AdapterChannelEnumerator channel_enumerator, AdapterTransportFactory transport_factory);

        [[nodiscard]] DiscoveryReport scan(const DiscoveryConfig &config) override;

    private:
        AdapterChannelEnumerator channel_enumerator_;
        AdapterTransportFactory transport_factory_;
        bool use_default_transport_ = false;
    };

    class MiniCANFDDiscoverySource final : public DiscoverySource {
    public:
        MiniCANFDDiscoverySource();
        MiniCANFDDiscoverySource(AdapterChannelEnumerator channel_enumerator,
                                 AdapterTransportFactory transport_factory);

        [[nodiscard]] DiscoveryReport scan(const DiscoveryConfig &config) override;

    private:
        AdapterChannelEnumerator channel_enumerator_;
        AdapterTransportFactory transport_factory_;
        bool use_default_transport_ = false;
    };

    // Runs discovery with an explicitly selected/injected source.
    [[nodiscard]] DiscoveryReport discover_hands(const DiscoveryConfig &config, DiscoverySource &source);

    // Uses the default PlatformDiscoverySource. On unsupported platforms the
    // report contains an enumeration failure instead of mutating host state.
    [[nodiscard]] DiscoveryReport discover_hands(const DiscoveryConfig &config = {});
} // namespace linkerbot::hand

#include <linkerbot/hand/adapter_transport.hpp>
#include <mutex>
#include <utility>
namespace linkerbot::hand {
#if LINKERBOT_HAND_HAS_SLCAN
    HandDevice::TransportFactory make_slcan_transport_factory(can::SlcanConfig config) {
        can::validate_slcan_config(config);
        config.port = can::slcan_port_name(config.port);
        return [config = std::move(config)] { return std::make_unique<can::SlcanTransport>(config); };
    }
#endif
    HandDevice::TransportFactory make_pcan_transport_factory(can::PcanConfig config) {
        can::validate_pcan_config(config);
        struct Binding {
            std::mutex mutex;
            std::shared_ptr<can::PcanApi> api;
        };
        auto binding = std::make_shared<Binding>();
        return [config = std::move(config), binding] {
            std::lock_guard<std::mutex> lock(binding->mutex);
            if (!binding->api) {
                binding->api = can::load_pcan_api(config.library_path);
            }
            return std::make_unique<can::PcanTransport>(config, binding->api);
        };
    }
    HandDevice::TransportFactory make_minicanfd_transport_factory(can::VendorCanFdTransport::Config config) {
        if (config.send_timeout_ms < 0 || config.send_timeout_ms > can::kVendorCanFdMaxSendTimeoutMs ||
            config.receive_timeout_ms < 0 || config.receive_timeout_ms > can::kVendorCanFdMaxReceiveTimeoutMs) {
            throw ValidationError("MiniCANFD factory timeout is out of range");
        }
#if defined(_WIN32)
        if (config.channel_index != 0) {
            throw ValidationError("Windows MiniCANFD channel_index must be 0");
        }
#endif
        struct Binding {
            std::mutex mutex;
            std::shared_ptr<can::VendorCanFdApi> api;
        };
        auto binding = std::make_shared<Binding>();
        return [config = std::move(config), binding] {
            std::lock_guard<std::mutex> lock(binding->mutex);
            if (!binding->api) {
                binding->api = can::load_vendor_canfd_api(config.library_path);
            }
            return std::make_unique<can::VendorCanFdTransport>(config, binding->api);
        };
    }
}

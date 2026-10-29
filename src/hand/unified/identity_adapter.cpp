#include "detail/model_adapters.hpp"

#include "../identity/probe.hpp"
#include "detail/common.hpp"

namespace linkerbot::hand::unified_detail {
    namespace {
        [[nodiscard]] identity_probe::Target classic_target(Model model, HandSide side) {
            return {model, side, classic_address(side), 0};
        }
    } // namespace

    DeviceIdentity read_identity(l6::L6 &device, HandSide side, std::chrono::milliseconds timeout) {
        return identity_probe::probe(device.runtime(), classic_target(Model::kL6, side), timeout);
    }

    DeviceIdentity read_identity(o6::O6 &device, HandSide side, std::chrono::milliseconds timeout) {
        return identity_probe::probe(device.runtime(), classic_target(Model::kO6, side), timeout);
    }

    DeviceIdentity read_identity(l20lite::L20lite &device, HandSide side, std::chrono::milliseconds timeout) {
        return identity_probe::probe(device.runtime(), classic_target(Model::kL20Lite, side), timeout);
    }

    DeviceIdentity read_identity(l20::L20 &device, HandSide side, std::chrono::milliseconds timeout) {
        return identity_probe::probe(device.runtime(), classic_target(Model::kL20, side), timeout);
    }

    DeviceIdentity read_identity(l30::L30 &device, HandSide side, std::chrono::milliseconds timeout) {
        return identity_probe::probe(device.runtime(), {Model::kL30, side, device.node_id(), device.host_id()},
                                     timeout);
    }

    DeviceIdentity read_identity(o30::O30 &device, HandSide side, std::chrono::milliseconds timeout) {
        return identity_probe::probe(device.runtime(), {Model::kO30, side, device.request_id(), 0}, timeout);
    }
} // namespace linkerbot::hand::unified_detail

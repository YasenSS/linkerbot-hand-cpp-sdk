#pragma once

#include <linkerbot/hand/capabilities.hpp>

namespace linkerbot::hand::capabilities_detail {
    [[nodiscard]] const DeviceCapabilities &l6_profile(HandSide side);
    [[nodiscard]] const DeviceCapabilities &o6_profile(HandSide side);
    [[nodiscard]] const DeviceCapabilities &l20lite_profile(HandSide side);
    [[nodiscard]] const DeviceCapabilities &l20_profile(HandSide side);
    [[nodiscard]] const DeviceCapabilities &l30_profile(HandSide side);
    [[nodiscard]] const DeviceCapabilities &o30_profile(HandSide side);

} // namespace linkerbot::hand::capabilities_detail

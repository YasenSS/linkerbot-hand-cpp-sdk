#include <linkerbot/hand/capabilities.hpp>

#include "profiles.hpp"

#include <stdexcept>

namespace linkerbot::hand {
    const DeviceCapabilities &declared_capabilities(Model model, HandSide side) {
        using namespace capabilities_detail;
        switch (model) {
        case Model::kL6:
            return l6_profile(side);
        case Model::kO6:
            return o6_profile(side);
        case Model::kL20Lite:
            return l20lite_profile(side);
        case Model::kL20:
            return l20_profile(side);
        case Model::kL30:
            return l30_profile(side);
        case Model::kO30:
            return o30_profile(side);
        case Model::kUnknown:
            break;
        }
        throw std::invalid_argument("unknown hand model");
    }

} // namespace linkerbot::hand

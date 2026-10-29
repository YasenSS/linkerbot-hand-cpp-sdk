#pragma once
#include <linkerbot/can/pcan_transport.hpp>
#include <linkerbot/can/vendor_canfd_transport.hpp>
#include <linkerbot/hand/api/device.hpp>
#if __has_include(<linkerbot/can/slcan_transport.hpp>)
#include <linkerbot/can/slcan_transport.hpp>
#define LINKERBOT_HAND_HAS_SLCAN 1
#else
#define LINKERBOT_HAND_HAS_SLCAN 0
#endif
#ifndef LINKERBOT_COMM_SLCAN_FD
#define LINKERBOT_COMM_SLCAN_FD 0
#endif
namespace linkerbot::hand {
    // Constructing these factories performs no driver loading/enumeration/I/O.
    // The first invocation resolves the module; subsequent opens/recoveries retain
    // that same API/module even if environment variables change.
    [[nodiscard]] HandDevice::TransportFactory make_pcan_transport_factory(can::PcanConfig config);
    [[nodiscard]] HandDevice::TransportFactory
    make_minicanfd_transport_factory(can::VendorCanFdTransport::Config config);
#if LINKERBOT_HAND_HAS_SLCAN
    // Explicit firmware profile, factory creation validates without serial I/O.
    [[nodiscard]] HandDevice::TransportFactory make_slcan_transport_factory(can::SlcanConfig config);
#endif
}

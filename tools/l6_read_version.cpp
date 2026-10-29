// Read-only device info probe for L6 right hand.
// No motion, no set_angles: only version/SN + a single angle snapshot.
#include <linkerbot/hand/l6/l6.hpp>

#include <chrono>
#include <iostream>
#include <string>

using namespace linkerbot::hand::l6;

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: l6_read_version [CAN_INTERFACE]\n"
                     "Read L6 right-hand version, serial number and one angle sample.\n"
                     "Default interface: can0. No motion commands are sent.\n";
        return 0;
    }
    if (argc > 2 || (argc == 2 && std::string(argv[1]).rfind("-", 0) == 0)) {
        std::cerr << "Usage: l6_read_version [CAN_INTERFACE]\n";
        return 1;
    }
    std::string interface = (argc > 1) ? argv[1] : "can0";

    L6 hand(Side::kRight, interface);

    std::cout << "=== L6 right-hand device info (interface " << interface << ") ===" << std::endl;

    try {
        auto info = hand.version.get_device_info(std::chrono::milliseconds{2000});
        std::cout << "serial_number   : " << info.serial_number << std::endl;
        std::cout << "firmware_version: " << info.firmware_version.to_string() << std::endl;
        std::cout << "pcb_version     : " << info.pcb_version.to_string() << std::endl;
        std::cout << "mechanical_ver  : " << info.mechanical_version.to_string() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "version read FAILED: " << e.what() << std::endl;
        return 2;
    }

    try {
        auto angle = hand.angle.get_blocking(std::chrono::milliseconds{500});
        auto a = angle.angle.to_array();
        std::cout << "angle_sample    : " << a[0] << " " << a[1] << " " << a[2] << " " << a[3] << " " << a[4] << " "
                  << a[5] << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "angle read FAILED: " << e.what() << std::endl;
        return 3;
    }

    std::cout << "OK" << std::endl;
    return 0;
}

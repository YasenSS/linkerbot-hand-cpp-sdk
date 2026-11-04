#include <iostream>
#include <linkerbot/hand/api/device.hpp>
#include <string>

// Explicit SocketCAN connection; never performs motion or writes configuration.
int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
        std::cout << "Usage: l6_hop_readonly <interface> <left|right> [request-id, default 0x001]\n"
                     "The interface must already support CAN FD at 1 Mbps; requests use no BRS.\n";
        return 0;
    }
    try {
        if (argc < 3 || argc > 4) {
            throw std::invalid_argument("expected interface, side and optional request ID");
        }
        using namespace linkerbot::hand;
        HandDeviceConfig config;
        config.model = Model::kL6;
        config.l6_protocol = L6Protocol::kHopV004;
        config.interface_name = argv[1];
        const std::string side = argv[2];
        if (side != "left" && side != "right") {
            throw std::invalid_argument("side must be left or right");
        }
        config.side = side == "left" ? HandSide::kLeft : HandSide::kRight;
        if (argc == 4) {
            size_t used = 0;
            const std::string input = argv[3];
            const auto id = std::stoul(input, &used, 0);
            if (used != input.size() || id > 0x3fe) {
                throw std::invalid_argument("request ID must be 0..0x3FE");
            }
            config.address = static_cast<uint32_t>(id);
        }
        HandDevice device(config);
        device.open();
        const auto identity = device.get_identity();
        if (!identity.has_value()) {
            throw std::runtime_error("device identity unavailable after open");
        }
        std::cout << identity->product_code.value_or("L6") << " HOP " << identity->protocol_version.value_or("")
                  << '\n';
        for (const auto value : device.get_angles().raw) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
        device.close();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

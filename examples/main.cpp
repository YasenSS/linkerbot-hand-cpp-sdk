#include <chrono>
#include <exception>
#include <iostream>
#include <string_view>
#include <thread>

#include <linkerbot/hand/l6/l6.hpp>

void run_l6_example() {
    namespace l6 = linkerbot::hand::l6;
    using namespace std::chrono_literals;

    // Linux SocketCAN: match the interface and side to the connected hand.
    l6::L6 hand{l6::Side::kRight, "can0"};

    const auto print_angles = [](const char *label, const l6::AngleData &sample) {
        std::cout << label;
        for (const auto value : sample.angle.to_array()) {
            std::cout << ' ' << value;
        }
        std::cout << " (%)\n";
    };
    print_angles("Angles before:", hand.angle.get_blocking(500ms));

    // L6 does not require enable(). Send one 50% target to all six joints.
    // This waits for protocol feedback, which does not imply mechanical arrival.
    hand.angle.set_angles_percent_blocking(l6::Angle{50, 50, 50, 50, 50, 50}, 500ms);
    std::this_thread::sleep_for(2s);
    print_angles("Angles after 2 seconds:", hand.angle.get_blocking(500ms));

    // Destruction closes the connection; the target is not restored.
}

int main(int argc, char **argv) try {
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help")) {
        std::cout << "Linux L6 example: right hand, SocketCAN can0, six joints at 50%.\n"
                     "Configure can0 for Classic CAN at 1 Mbit/s before running.\n"
                     "Usage: example --enable-motion\n";
        return 0;
    }
    if (argc != 2 || std::string_view{argv[1]} != "--enable-motion") {
        std::cerr << "Usage: example --enable-motion (Linux only)\n";
        return 2;
    }

#if defined(__linux__)
    run_l6_example();
    return 0;
#else
    std::cerr << "This example uses Linux SocketCAN. On Windows, use windows_readonly\n"
                 "or the adapter factories documented in docs/windows-adapters.md.\n";
    return 1;
#endif
} catch (const std::exception &error) {
    std::cerr << "L6 example failed: " << error.what() << '\n';
    return 1;
}

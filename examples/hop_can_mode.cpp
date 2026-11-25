#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <linkerbot/hand/api/device.hpp>

namespace {
    constexpr const char *kUsage =
        "Usage: hop_can_mode <interface> <o6|l6|o30> <left|right> [--request-id 0x001] "
        "[--from-mode canfd|can] [--mode can|canfd] [--save]\n"
        "Without --mode the tool only reads identity and MI 0x35 CAN configuration.\n"
        "--from-mode can is valid for O6. L6 and O30 open a CAN FD socket.\n"
        "--save stores the verified mode with MI 0x35 SI 0x1E and requires --mode.\n"
        "Exit 2 means the register matches the target but the active wire format is still pending a restart.\n";

    struct Options {
        std::string interface;
        std::string model;
        std::string side;
        uint32_t request_id = 0x001;
        std::string from_mode = "canfd";
        std::optional<std::string> mode;
        bool save = false;
    };

    [[nodiscard]] std::string require_value(int argc, char **argv, int &index, std::string_view name) {
        if (index + 1 >= argc) {
            throw std::invalid_argument(std::string(name) + " requires a value");
        }
        ++index;
        return argv[index];
    }

    [[nodiscard]] Options parse(int argc, char **argv) {
        Options options;
        int positionals = 0;
        for (int index = 1; index < argc; ++index) {
            const std::string arg = argv[index];
            if (arg == "--request-id") {
                const auto text = require_value(argc, argv, index, arg);
                size_t used = 0;
                const auto id = std::stoul(text, &used, 0);
                if (used != text.size() || id > 0x3FEU) {
                    throw std::invalid_argument("request ID must be 0..0x3FE");
                }
                options.request_id = static_cast<uint32_t>(id);
            } else if (arg == "--from-mode") {
                options.from_mode = require_value(argc, argv, index, arg);
                if (options.from_mode != "can" && options.from_mode != "canfd") {
                    throw std::invalid_argument("--from-mode must be can or canfd");
                }
            } else if (arg == "--mode") {
                options.mode = require_value(argc, argv, index, arg);
                if (*options.mode != "can" && *options.mode != "canfd") {
                    throw std::invalid_argument("--mode must be can or canfd");
                }
            } else if (arg == "--save") {
                options.save = true;
            } else if (!arg.empty() && arg[0] == '-') {
                throw std::invalid_argument("unknown option " + arg);
            } else if (positionals == 0) {
                options.interface = arg;
                ++positionals;
            } else if (positionals == 1) {
                options.model = arg;
                ++positionals;
            } else if (positionals == 2) {
                options.side = arg;
                ++positionals;
            } else {
                throw std::invalid_argument("unexpected argument " + arg);
            }
        }
        if (positionals != 3) {
            throw std::invalid_argument("expected interface, model and side");
        }
        if (options.model != "o6" && options.model != "l6" && options.model != "o30") {
            throw std::invalid_argument("model must be o6, l6 or o30");
        }
        if (options.side != "left" && options.side != "right") {
            throw std::invalid_argument("side must be left or right");
        }
        if (options.save && !options.mode) {
            throw std::invalid_argument("--save requires --mode");
        }
        if (options.from_mode == "can" && options.model != "o6") {
            throw std::invalid_argument("--from-mode can is only valid for o6");
        }
        return options;
    }

    [[nodiscard]] const char *model_name(linkerbot::hand::Model model) {
        using linkerbot::hand::Model;
        switch (model) {
        case Model::kO6:
            return "O6";
        case Model::kL6:
            return "L6";
        case Model::kO30:
            return "O30";
        case Model::kL20:
            return "L20";
        case Model::kL20Lite:
            return "L20Lite";
        case Model::kL30:
            return "L30";
        case Model::kUnknown:
            return "unknown";
        }
        return "unknown";
    }

    [[nodiscard]] const char *side_name(linkerbot::hand::HandSide side) {
        using linkerbot::hand::HandSide;
        switch (side) {
        case HandSide::kLeft:
            return "left";
        case HandSide::kRight:
            return "right";
        case HandSide::kUnknown:
            return "unknown";
        }
        return "unknown";
    }

    void print_configuration(const linkerbot::hand::HopCanConfiguration &configuration) {
        std::cout << "request_id=0x" << std::hex << configuration.request_id << std::dec << " response_id=0x"
                  << std::hex << configuration.response_id() << std::dec << " mode="
                  << (configuration.interface_mode == linkerbot::hand::HopCanInterfaceMode::kCanFd ? "canfd" : "can")
                  << " brs=" << (configuration.brs ? 1 : 0) << " nominal=" << configuration.nominal_bitrate
                  << " data=" << configuration.data_bitrate << '\n';
    }
} // namespace

int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
        std::cout << kUsage;
        return 0;
    }
    try {
        const auto options = parse(argc, argv);
        using namespace linkerbot::hand;
        HandDeviceConfig config;
        config.interface_name = options.interface;
        config.side = options.side == "left" ? HandSide::kLeft : HandSide::kRight;
        config.address = options.request_id;
        if (options.model == "o6") {
            config.model = Model::kO6;
            config.o6_protocol = options.from_mode == "can" ? O6Protocol::kHopV004Can : O6Protocol::kHopV004;
        } else if (options.model == "l6") {
            config.model = Model::kL6;
            config.l6_protocol = L6Protocol::kHopV004;
        } else {
            config.model = Model::kO30;
        }

        constexpr auto kTimeout = std::chrono::milliseconds{500};
        HandDevice hand(config);
        hand.open();
        const auto identity = hand.get_identity();
        if (!identity) {
            throw std::runtime_error("device identity unavailable after open");
        }
        std::cout << model_name(identity->model) << ' ' << side_name(identity->side)
                  << " serial=" << identity->serial_number.value_or("")
                  << " protocol=" << identity->protocol_name.value_or("")
                  << " version=" << identity->protocol_version.value_or("") << '\n';

        int status = 0;
        if (!options.mode) {
            print_configuration(hand.get_can_configuration(kTimeout));
        } else {
            const auto target = *options.mode == "canfd" ? HopCanInterfaceMode::kCanFd : HopCanInterfaceMode::kCan20;
            const auto result = hand.set_can_interface_mode(target, options.save, kTimeout);
            std::cout << "before ";
            print_configuration(result.before);
            std::cout << "after ";
            print_configuration(result.after);
            std::cout << "write_sent=" << (result.write_sent ? "yes" : "no") << '\n';
            if (options.save) {
                std::cout << (result.saved_to_flash ? "Flash save confirmed ONCE; power-cycle persistence untested\n"
                                                    : "Flash save was not confirmed\n");
            }
            const bool target_fd = target == HopCanInterfaceMode::kCanFd;
            if (result.active_wire_is_fd == target_fd) {
                std::cout << "ACTIVE: target register and actual reply format verified\n";
            } else {
                std::cout << "PENDING: target register verified, active format unchanged. "
                             "No automatic restart; verify after a controlled restart.\n";
                status = 2;
            }
        }
        hand.close();
        return status;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

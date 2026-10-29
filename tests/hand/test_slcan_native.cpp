#include <linkerbot/hand/adapter_transport.hpp>
#if defined(__linux__) && LINKERBOT_HAND_HAS_SLCAN && LINKERBOT_COMM_SLCAN_FD
#include "../../validation/fake_classic_hand.hpp"
#include "hand/l30/comm_helpers.hpp"
#include "hand/o30/comm_helpers.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fcntl.h>
#include <linkerbot/hand/discovery.hpp>
#include <map>
#include <poll.h>
#include <thread>
#include <unistd.h>

using namespace linkerbot::hand;
using namespace std::chrono_literals;
namespace {
    // An actual Linux PTY serves CANable ASCII, without serial ACKs. Only the
    // emulated hand replies, so discovery/reopen/HandDevice cross the native I/O.
    class SerialHand {
        using Message = linkerbot::can::Message;
        int master_ = -1;
        std::atomic<bool> stopping_{false};
        std::thread worker_;
        std::exception_ptr error_;
        std::map<uint32_t, linkerbot::can::FakeTransport::Handler> handlers_;
        o30_comm::HopEndpoint hop_;
        Model model_;
        bool lawicel_;
        void serve() {
            try {
                std::string line;
                while (!stopping_) {
                    pollfd descriptor{master_, POLLIN, 0};
                    if (::poll(&descriptor, 1, 10) <= 0 || !(descriptor.revents & POLLIN)) {
                        std::this_thread::sleep_for(1ms);
                        continue;
                    }
                    char bytes[256];
                    const auto count = ::read(master_, bytes, sizeof(bytes));
                    for (ssize_t i = 0; i < count; ++i) {
                        if (bytes[i] != '\r') {
                            line += bytes[i];
                            continue;
                        }
                        if (!line.empty() && std::string{"tTdDbB"}.find(line[0]) != std::string::npos) {
                            if (lawicel_ && ::write(master_, "\r", 1) != 1) {
                                throw std::runtime_error("PTY ACK failed");
                            }
                            const auto request =
                                linkerbot::can::decode_slcan_frame(line, linkerbot::can::SlcanProtocol::kCanableFd);
                            std::vector<Message> replies;
                            if (model_ == Model::kO30 && request.id == hop_.request_id) {
                                replies = hop_.reply(request);
                            } else if (const auto handler = handlers_.find(request.id); handler != handlers_.end()) {
                                replies = handler->second(request);
                            }
                            for (const auto &reply : replies) {
                                const auto text = linkerbot::can::encode_slcan_frame(
                                    reply, linkerbot::can::SlcanProtocol::kCanableFd);
                                // Split the line to exercise fragmented serial reads.
                                for (size_t offset = 0; offset < text.size();) {
                                    const auto sent = ::write(master_, text.data() + offset,
                                                              std::min(size_t{7}, text.size() - offset));
                                    if (sent > 0) {
                                        offset += static_cast<size_t>(sent);
                                    } else {
                                        throw std::runtime_error("Emulated serial reply failed");
                                    }
                                }
                            }
                        } else if (lawicel_ && !line.empty() && ::write(master_, "\r", 1) != 1) {
                            throw std::runtime_error("PTY startup ACK failed");
                        }
                        line.clear();
                    }
                }
            } catch (...) {
                error_ = std::current_exception();
            }
        }

    public:
        std::string path;
        explicit SerialHand(Model model, bool lawicel = false) : model_(model), lawicel_(lawicel) {
            master_ = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
            try {
                if (master_ < 0 || ::grantpt(master_) || ::unlockpt(master_)) {
                    throw std::runtime_error("Cannot create serial hand PTY");
                }
                char name[128]{};
                if (::ptsname_r(master_, name, sizeof(name))) {
                    throw std::runtime_error("Cannot name serial hand PTY");
                }
                path = name;
                if (model == Model::kL30) {
                    namespace p = linkerbot::hand::l30::protocol;
                    handlers_[l30_comm::request_id(p::kParentConfig, p::kSubDeviceInfo, p::kAccessRead)] =
                        l30_comm::payload_handler({0x13, 0, 0, 0, 0x7B, 1, 2, 3, 2, 3, 4, 3, 4, 5, 1, 0, 'B', 'A'});
                    const std::string code = "LHT30-06-123-L-B-3-A";
                    handlers_[l30_comm::request_id(p::kParentConfig, p::kSubProductCode, p::kAccessRead)] =
                        l30_comm::payload_handler({code.begin(), code.end()});
                    std::array<int16_t, 17> angles{};
                    angles.fill(100);
                    handlers_[l30_comm::query_id(p::kSubPosition)] = l30_comm::i16_handler(angles);
                    handlers_[l30_comm::query_id(p::kSubFault)] = l30_comm::u8_handler({});
                } else if (model == Model::kO30) {
                    hop_.seed_defaults();
                } else {
                    handlers_[0x28] = validation::l6_reply;
                }
                worker_ = std::thread([this] { serve(); });
            } catch (...) {
                if (master_ >= 0) {
                    ::close(master_);
                }
                throw;
            }
        }
        ~SerialHand() {
            stop();
            if (master_ >= 0) {
                ::close(master_);
            }
        }
        void stop() {
            stopping_ = true;
            if (worker_.joinable()) {
                worker_.join();
            }
        }
        void check() {
            stop();
            if (error_) {
                std::rethrow_exception(error_);
            }
        }
    };
}
TEST_CASE("Linux SLCAN FD discovers and reopens L30 and O30 through native serial",
          "[comm][discovery][slcan][native-serial]") {
    for (const auto model : {Model::kL30, Model::kO30}) {
        CAPTURE(model);
        SerialHand serial(model);
        DiscoveryConfig config;
        config.total_timeout = 3s;
        config.response_timeout = 100ms;
        config.send_timeout = 200ms;
        config.inter_request_delay = 0ms;
        config.l30_node_ids = {1};
        config.o30_request_ids = {1};
        config.slcan.channels = {serial.path};
        config.slcan.protocol = SLCANProtocol::kCanableFd;
        config.slcan.probe_protocols = {TransportKind::kCanFd};
        config.slcan.serial_baud = 460800;
        SLCANDiscoverySource source;
        const auto report = discover_hands(config, source);
        for (const auto &failure : report.failures()) {
            UNSCOPED_INFO(failure.message());
        }
        for (const auto &candidate : report.candidates()) {
            UNSCOPED_INFO("candidate model=" << int(candidate.model()) << " status=" << int(candidate.status()));
        }
        UNSCOPED_INFO("channels=" << report.channels().size());
        REQUIRE(report.candidates().size() == 1);
        REQUIRE(report.verified_candidates().size() == (model == Model::kL30 ? 1 : 0));
        const auto &candidate = report.require_one(true);
        REQUIRE(candidate.model() == model);
        REQUIRE(candidate.channel().native_name() == serial.path);
        // Candidate captures the exact port/profile. Reopen twice to prove the
        // discovery transport relinquishes exclusive serial ownership.
        auto device = candidate.create_hand_device(HandRuntimeConfig{}, HandSide::kLeft);
        for (int round = 0; round < 2; ++round) {
            device->open();
            const auto angles = device->get_angles(1s);
            REQUIRE(angles.meta.valid);
            REQUIRE(angles.raw.size() == (model == Model::kL30 ? 17 : 20));
            REQUIRE(device->get_device_info().model == model);
            device->close();
        }
        REQUIRE_NOTHROW(serial.check());
    }
}
TEST_CASE("Linux Lawicel serial works with continuous HandDevice receive", "[comm][slcan][native-serial]") {
    SerialHand serial(Model::kL6, true);
    linkerbot::can::SlcanConfig adapter;
    adapter.port = serial.path;
    HandDevice device(validation::config(), make_slcan_transport_factory(adapter));
    device.open();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(device.get_angles(1s).meta.valid);
    }
    device.close();
    REQUIRE_NOTHROW(serial.check());
}
#endif

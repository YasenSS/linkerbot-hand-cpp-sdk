#pragma once
// CAN FD transport wrapper for standalone O30 acceptance tests.
// Records exact requests and responses; build/run instructions are in README.md.
#include <fstream>
#include <iomanip>
#include <iostream>
#include <linkerbot/can/socketcan_transport.hpp>
#include <linkerbot/hand/o30/o30.hpp>
#include <mutex>
#include <string>

using namespace linkerbot::hand::o30;
using namespace std::chrono_literals;

class TracedTransport final : public linkerbot::can::CANTransport {
    linkerbot::can::SocketCANTransport inner_;
    std::ofstream trace_;
    std::mutex mutex_;
    const std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    void log(const char *direction, const linkerbot::can::Message &m) {
        const std::lock_guard<std::mutex> lock(mutex_);
        trace_
            << std::dec
            << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_).count()
            << "us " << direction << " id=" << std::hex << m.id << std::dec << " fd=" << m.is_fd << " brs=" << m.is_brs
            << " ext=" << m.is_extended << " len=" << +m.dlc << " data=";
        for (size_t i = 0; i < m.dlc; ++i) {
            trace_ << std::hex << std::setfill('0') << std::setw(2) << +m.data[i];
        }
        trace_ << std::dec << std::endl;
    }

public:
    TracedTransport(const char *iface, const char *path) : inner_(iface, true), trace_(path) {
        if (!trace_) {
            throw std::runtime_error("cannot open frame trace");
        }
    }
    std::optional<linkerbot::can::Message> read() override {
        auto m = inner_.read();
        if (m) {
            log("RX", *m);
        }
        return m;
    }
    void write(const linkerbot::can::Message &m) override {
        log("TX_ATTEMPT", m);
        inner_.write(m);
    }
    void close() override { inner_.close(); }
};

template <class T> void values(const T &v) {
    std::cout << '[';
    bool first = true;
    for (const auto &x : v) {
        if (!first) {
            std::cout << ',';
        }
        first = false;
        std::cout << +x;
    }
    std::cout << ']';
}

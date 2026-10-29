#pragma once
#include "hand_device_angle_baseline.hpp"
#include "hand_device_hardware_fixture.hpp"
#include <atomic>
#include <condition_variable>
#include <linkerbot/can/transport.hpp>
#include <linkerbot/transport/session.hpp>
#include <mutex>
#include <thread>

namespace linkerbot::hand::hardware_test {
    struct TransportProbe {
        std::mutex mutex;
        std::condition_variable changed;
        bool gate = false, entered = false, release = false, fail_blocked = false;
        std::atomic<bool> reject_writes{false}, suppress_reads{false};
        std::atomic<uint64_t> attempts{0};
        std::vector<can::Message> sent;
        std::optional<can::Message> blocked;

        void arm() {
            const std::lock_guard<std::mutex> lock(mutex);
            gate = true;
            entered = false;
            release = false;
            fail_blocked = false;
        }
        bool wait_entered() {
            std::unique_lock<std::mutex> lock(mutex);
            return changed.wait_for(lock, std::chrono::seconds{2}, [&] { return entered; });
        }
        void unblock(bool fail = false) {
            const std::lock_guard<std::mutex> lock(mutex);
            fail_blocked = fail;
            release = true;
            gate = false;
            changed.notify_all();
        }
        std::vector<can::Message> successful_writes() {
            const std::lock_guard<std::mutex> lock(mutex);
            return sent;
        }
    };

    // Frames still use the selected real adapter. Fault injection is explicitly
    // labelled; it never pretends to be a physical cable-disconnect test.
    class ProbedTransport final : public can::CANTransport {
    public:
        ProbedTransport(std::unique_ptr<can::CANTransport> native, std::shared_ptr<TransportProbe> probe)
            : native_(std::move(native)), probe_(std::move(probe)) {}
        std::optional<can::Message> read() override {
            auto message = native_->read();
            if (probe_->suppress_reads.load()) {
                return std::nullopt;
            }
            return message;
        }
        void write(const can::Message &message) override {
            ++probe_->attempts;
            {
                std::unique_lock<std::mutex> lock(probe_->mutex);
                if (probe_->gate) {
                    probe_->entered = true;
                    probe_->blocked = message;
                    probe_->changed.notify_all();
                    if (!probe_->changed.wait_for(lock, std::chrono::seconds{3},
                                                  [&] { return probe_->release || closed_.load(); })) {
                        throw can::TransportIOError("hardware test write gate timed out");
                    }
                    if (probe_->fail_blocked) {
                        throw can::TransportIOError("injected blocked-write failure before physical send");
                    }
                }
            }
            if (closed_.load() || probe_->reject_writes.load()) {
                throw can::TransportIOError("injected write failure before physical send");
            }
            native_->write(message);
            const std::lock_guard<std::mutex> lock(probe_->mutex);
            probe_->sent.push_back(message);
        }
        void close() override {
            closed_ = true;
            probe_->changed.notify_all();
            native_->close();
        }

    private:
        std::unique_ptr<can::CANTransport> native_;
        std::shared_ptr<TransportProbe> probe_;
        std::atomic<bool> closed_{false};
    };

    inline HandDevice::TransportFactory probe_factory(const HandDeviceConfig &config,
                                                      std::shared_ptr<TransportProbe> probe) {
        auto native = hardware_factory(config);
        return [native, probe] { return std::make_unique<ProbedTransport>(native(), probe); };
    }

    struct ProbedFixture {
        HandDeviceConfig config;
        std::shared_ptr<TransportProbe> probe = std::make_shared<TransportProbe>();
        std::shared_ptr<transport::Session> session;
        std::unique_ptr<o30::O30> setpoint_reader;
        std::unique_ptr<HandDevice> hand;
        explicit ProbedFixture(bool shared = false, std::chrono::milliseconds timeout = std::chrono::seconds{2})
            : config(config_from_environment()) {
            config.command_timeout = timeout;
            auto factory = probe_factory(config, probe);
            if (shared || config.model == Model::kO30) {
                session = transport::Session::create(factory);
                hand = std::make_unique<HandDevice>(config, session);
            } else {
                hand = std::make_unique<HandDevice>(config, factory);
            }
        }
        ~ProbedFixture() {
            probe->suppress_reads = false;
            probe->reject_writes = false;
            probe->unblock(true);
            try {
                hand->close();
            } catch (...) {
            }
            try {
                if (session) {
                    session->close();
                }
            } catch (...) {
            }
        }
        o30::O30 *get_setpoint_reader() {
            if (config.model != Model::kO30) {
                return nullptr;
            }
            if (!setpoint_reader) {
                setpoint_reader =
                    std::make_unique<o30::O30>(session, config.address.value_or(o30::protocol::kDefaultRequestId));
            }
            return setpoint_reader.get();
        }
    };

    inline std::vector<int32_t> stationary_pose(HandDevice &hand, const HandDeviceConfig &config,
                                                std::vector<double> *measured = nullptr, o30::O30 *reader = nullptr) {
        const auto sample = hand.get_angles(std::chrono::milliseconds{500});
        const auto caps = hand.get_capabilities();
        require_meta(sample.meta, hand.get_health().connection_generation);
        REQUIRE(sample.raw.size() == caps.joints.size());
        if (measured) {
            *measured = sample.raw;
        }
        const auto configured = o30_setpoint_pose(config, reader);
        if (configured) {
            REQUIRE(configured->size() == sample.raw.size());
        }
        std::vector<int32_t> result;
        for (size_t i = 0; i < sample.raw.size(); ++i) {
            REQUIRE(std::isfinite(sample.raw[i]));
            const auto value = configured ? (*configured)[i] : std::round(sample.raw[i]);
            if (!caps.joints[i].raw_angle_range.contains(value)) {
                SKIP("prepare an in-range pose using angle acceptance before stationary command contracts");
            }
            if (configured) {
                const auto &range = caps.joints[i].raw_angle_range;
                REQUIRE(std::abs(sample.raw[i] - value) <= std::max(0.5, (range.maximum - range.minimum) * 0.10));
            }
            result.push_back(static_cast<int32_t>(value));
        }
        return result;
    }
}

#include "../../../src/hand/unified/detail/python_stage_scheduling.hpp"
#include "../../../src/hand/unified/detail/python_state.hpp"
#include "../../../src/hand/unified/detail/python_transactions.hpp"
#include "../../../src/hand/unified/detail/read_coordinator.hpp"
#include "../../../validation/fake_classic_hand.hpp"
#include "hand/l30/comm_helpers.hpp"
#include "hand/o30/comm_helpers.hpp"
#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>
#include <condition_variable>
#include <future>
#include <linkerbot/hand/l30/l30.hpp>
#include <mutex>

using namespace linkerbot::hand;
using namespace std::chrono_literals;
namespace {
    using Message = linkerbot::can::Message;
    struct Script {
        std::promise<void> first_finger;
        std::promise<void> resume;
        std::shared_future<void> release = resume.get_future().share();
        std::atomic<bool> gate{false};
        std::atomic<bool> slow_angles{false};
        std::atomic<bool> bad_angles{false};
        std::atomic<bool> missing_row{false};
        std::atomic<uint8_t> held_finger{0};
        std::atomic<bool> hold_angle{false};
        std::atomic<int> feedback_mode{0}; // actual / missing / malformed / transport failure
        std::atomic<unsigned> angle_queries{0};
        std::atomic<unsigned> angle_writes{0};
        std::atomic<unsigned> finger_requests{0};
        std::function<void(const Message &)> on_angle;
        Model model;
        explicit Script(Model value) : model(value) {}
        std::vector<Message> reply(const Message &request) {
            const auto opcode = request.data[0];
            if (opcode >= 0xB1 && opcode <= 0xB5) {
                ++finger_requests;
                if (held_finger == opcode) {
                    return {};
                }
                if (opcode == 0xB1 && gate.exchange(false)) {
                    first_finger.set_value();
                    release.wait();
                }
                std::vector<Message> rows;
                const size_t count = model == Model::kO6 ? 10 : 12;
                for (size_t row = 0; row < count; ++row) {
                    if (missing_row && opcode == 0xB1 && row == count - 1) {
                        continue;
                    }
                    auto frame = request;
                    frame.dlc = model == Model::kO6 ? 6 : 8;
                    frame.data[1] = static_cast<uint8_t>(row);
                    std::fill(frame.data.begin() + 2, frame.data.begin() + frame.dlc, opcode);
                    rows.push_back(frame);
                }
                return rows;
            }
            if (opcode == 0xC0) {
                const std::string code = model == Model::kL6    ? "LHL6-1-123-L"
                                         : model == Model::kO6  ? "LHO6-1-123-L"
                                         : model == Model::kL20 ? "LHT20-1-123-L"
                                                                : "LHT10-1-123-L";
                std::vector<Message> frames;
                for (size_t chunk = 0; chunk < 4; ++chunk) {
                    auto frame = request;
                    frame.dlc = 8;
                    frame.data[1] = static_cast<uint8_t>(model == Model::kL20Lite ? chunk * 6 : chunk);
                    for (size_t index = 0; index < 6; ++index) {
                        const auto offset = chunk * 6 + index;
                        frame.data[index + 2] = offset < code.size() ? static_cast<uint8_t>(code[offset]) : 0;
                    }
                    frames.push_back(frame);
                }
                return frames;
            }
            if (opcode == 0xC1 || opcode == 0xC2 || opcode == 0xC4) {
                auto frame = request;
                frame.dlc = model == Model::kL6 && opcode == 0xC1 ? 5 : 4;
                frame.data[1] = 1;
                frame.data[2] = 2;
                frame.data[3] = 3;
                return {frame};
            }
            if (opcode == 1 && request.dlc == 1) {
                ++angle_queries;
                if (on_angle) {
                    on_angle(request);
                }
                if (hold_angle) {
                    return {};
                }
            }
            if (opcode == 1 && request.dlc > 1) {
                ++angle_writes;
                if (on_angle) {
                    on_angle(request);
                }
                if (feedback_mode == 3) {
                    throw linkerbot::can::TransportIOError("nested angle physical send failed");
                }
                if (feedback_mode == 1) {
                    return {};
                }
                if (feedback_mode == 2) {
                    auto bad = request;
                    bad.dlc = 2;
                    return {bad};
                }
            }
            if (slow_angles && request.dlc == 1) {
                std::this_thread::sleep_for(3ms);
            }
            auto frame = request;
            frame.dlc = bad_angles && request.dlc == 1 ? 2
                        : model == Model::kL20Lite     ? (opcode == 1   ? 7
                                                          : opcode == 4 ? 5
                                                                        : 6)
                                                       : 7;
            for (size_t index = 1; index < frame.dlc; ++index) {
                frame.data[index] = request.dlc > 1 ? request.data[index] : 64;
            }
            return {frame};
        }
    };
    struct Device {
        std::shared_ptr<Script> script;
        linkerbot::can::FakeTransport *transport = nullptr;
        HandDevice hand;
        explicit Device(HandDeviceConfig config)
            : script(std::make_shared<Script>(config.model)), hand(config, [this] {
                  auto fake = std::make_unique<linkerbot::can::FakeTransport>();
                  transport = fake.get();
                  fake->on_request(0x28, [state = script](const auto &request) { return state->reply(request); });
                  return fake;
              }) {
            hand.open();
            transport->clear_sent();
            script->angle_queries = 0;
            script->angle_writes = 0;
            script->finger_requests = 0;
        }
        explicit Device(Model model, RuntimeBehavior behavior = RuntimeBehavior::kPython)
            : Device(configuration(model, behavior)) {}
        static HandDeviceConfig configuration(Model model, RuntimeBehavior behavior) {
            auto config = validation::config();
            config.model = model;
            config.runtime_behavior = behavior;
            config.response_timeout = behavior == RuntimeBehavior::kPython ? std::optional{100ms} : std::nullopt;
            config.identity_timeout = 200ms;
            return config;
        }
    };
    template <typename Predicate> bool observed(Predicate predicate) {
        const auto deadline = runtime::Clock::now() + 2s;
        while (!predicate()) {
            if (runtime::Clock::now() >= deadline) {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }
    ChannelMetrics angle_metrics(const HandHealth &health) {
        const auto found = std::find_if(health.channels.begin(), health.channels.end(),
                                        [](const auto &channel) { return channel.channel == "angles"; });
        if (found != health.channels.end()) {
            return *found;
        }
        throw std::runtime_error("angle health channel missing");
    }
    bool closed_read(std::future<ForceSample> &result) {
        try {
            (void)result.get();
        } catch (const ClosedError &) {
            return true;
        } catch (const DeviceNotReadyError &) {
            return true;
        }
        return false;
    }
    void inject_finger(Device &device, uint8_t opcode) {
        Message request{};
        request.id = 0x28;
        request.dlc = 2;
        request.data[0] = opcode;
        const auto held = device.script->held_finger.exchange(0);
        for (const auto &reply : device.script->reply(request)) {
            device.transport->inject(reply);
        }
        device.script->held_finger = held;
    }
    void inject_angles(Device &device, uint8_t value = 64) {
        Message reply{};
        reply.id = 0x28;
        reply.dlc = 7;
        reply.data[0] = 1;
        std::fill_n(reply.data.begin() + 1, 6, value);
        device.transport->inject(reply);
    }
    std::vector<Message> between_first_fingers(const std::vector<Message> &sent) {
        const auto first = std::find_if(sent.begin(), sent.end(), [](const auto &m) { return m.data[0] == 0xB1; });
        const auto second = std::find_if(first, sent.end(), [](const auto &m) { return m.data[0] == 0xB2; });
        REQUIRE(first != sent.end());
        REQUIRE(second != sent.end());
        return {first + 1, second};
    }
}

TEST_CASE("Python tactile boundaries service complete angle batches under all feedback policies",
          "[comm][python-phase5][tactile-scheduling]") {
    for (const auto model : {Model::kL6, Model::kO6, Model::kL20Lite, Model::kL20}) {
        for (const auto policy : {AngleFeedbackPolicy::kQueryOnly, AngleFeedbackPolicy::kCommandFeedbackOnly,
                                  AngleFeedbackPolicy::kAdaptive}) {
            CAPTURE(model, policy);
            Device device(model);
            device.script->gate = true;
            auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
            const bool entered = device.script->first_finger.get_future().wait_for(1s) == std::future_status::ready;
            auto replaced =
                device.hand.set_angles(std::vector<int32_t>(device.hand.get_capabilities().joints.size(), 10));
            auto command =
                device.hand.set_angles(std::vector<int32_t>(device.hand.get_capabilities().joints.size(), 20));
            device.hand.start_polling({{TelemetrySemantic::kAngles, 1s, policy}});
            device.script->resume.set_value();
            const auto sample = force.get();
            validation::complete(command);
            device.hand.stop_polling();
            REQUIRE(entered);
            REQUIRE(replaced.stage() == CommandStage::kSuperseded);
            REQUIRE(sample.tactile.size() == 5);
            REQUIRE(sample.meta.sequence == 1);
            const auto interstage = between_first_fingers(device.transport->sent());
            const size_t frames = model == Model::kL20 ? 5 : model == Model::kL20Lite ? 2 : 1;
            const auto writes =
                std::count_if(interstage.begin(), interstage.end(), [](const auto &m) { return m.dlc > 1; });
            const auto queries =
                std::count_if(interstage.begin(), interstage.end(), [](const auto &m) { return m.dlc == 1; });
            REQUIRE(writes == frames);
            const bool query_required =
                policy == AngleFeedbackPolicy::kQueryOnly ||
                (policy == AngleFeedbackPolicy::kAdaptive &&
                 !device.hand.get_capabilities().command(CommandSemantic::kAngles)->returns_current_state);
            REQUIRE(queries == (query_required ? frames : 0));
        }
    }
}

TEST_CASE("Python continuous angle polls are bounded to four transactions per finger boundary",
          "[comm][python-phase5][tactile-scheduling]") {
    Device device(Model::kL6);
    device.script->gate = true;
    auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
    const bool entered = device.script->first_finger.get_future().wait_for(1s) == std::future_status::ready;
    device.script->slow_angles = true;
    device.hand.start_polling({{TelemetrySemantic::kAngles, 1ms, AngleFeedbackPolicy::kQueryOnly}});
    device.script->resume.set_value();
    const auto sample = force.get();
    device.hand.stop_polling();
    REQUIRE(entered);
    REQUIRE(sample.tactile.size() == 5);
    const auto sent = device.transport->sent();
    const auto interstage = between_first_fingers(sent);
    REQUIRE(interstage.size() == 4);
    for (uint8_t opcode = 0xB1; opcode < 0xB5; ++opcode) {
        const auto first =
            std::find_if(sent.begin(), sent.end(), [opcode](const auto &m) { return m.data[0] == opcode; });
        const auto next = std::find_if(first, sent.end(), [opcode](const auto &m) { return m.data[0] == opcode + 1; });
        REQUIRE(first != sent.end());
        REQUIRE(next != sent.end());
        // Each new complete finger resets the cap, not each new poll demand.
        REQUIRE(std::distance(first, next) == 5);
    }
    REQUIRE(
        std::all_of(interstage.begin(), interstage.end(), [](const auto &m) { return m.data[0] == 1 && m.dlc == 1; }));
    REQUIRE(std::count_if(sent.begin(), sent.end(),
                          [](const auto &m) { return m.data[0] >= 0xB1 && m.data[0] <= 0xB5; }) == 5);
}

TEST_CASE("Native tactile reads retain whole-operation command exclusion", "[comm][tactile-scheduling]") {
    for (const auto model : {Model::kL6}) {
        CAPTURE(model);
        Device device(model, RuntimeBehavior::kNative);
        device.script->gate = true;
        auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
        const bool entered = device.script->first_finger.get_future().wait_for(1s) == std::future_status::ready;
        auto command = device.hand.set_angles(std::vector<int32_t>(device.hand.get_capabilities().joints.size(), 20));
        device.script->resume.set_value();
        (void)force.get();
        validation::complete(command);
        REQUIRE(entered);
        REQUIRE(between_first_fingers(device.transport->sent()).empty());
    }
}

TEST_CASE("An incomplete finger cannot yield to angle work or replace the full tactile cache",
          "[comm][python-phase5][tactile-scheduling]") {
    Device device(Model::kL20);
    const auto previous = device.hand.get_force(3s);
    device.script->missing_row = true;
    device.transport->clear_sent();
    REQUIRE_THROWS_AS(device.hand.get_force(3s), TimeoutError);
    const auto sent = device.transport->sent();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent.front().data[0] == 0xB1);
    REQUIRE(test_support::require_value(device.hand.get_cached_snapshot().force).meta.sequence ==
            previous.meta.sequence);
}

TEST_CASE("Nested angle protocol failure releases its readers and tactile continues in the same generation",
          "[comm][python-phase5][tactile-scheduling]") {
    Device device(Model::kL6);
    device.script->gate = true;
    auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
    const bool entered = device.script->first_finger.get_future().wait_for(1s) == std::future_status::ready;
    device.script->bad_angles = true;
    device.hand.start_polling({{TelemetrySemantic::kAngles, 1s, AngleFeedbackPolicy::kQueryOnly}});
    device.script->resume.set_value();
    const auto result = force.get();
    device.hand.stop_polling();
    REQUIRE(entered);
    REQUIRE(result.tactile.size() == 5);
    REQUIRE(between_first_fingers(device.transport->sent()).size() == 1);
}

TEST_CASE("Python stage budget rejects equality and preserves finite execution deadlines",
          "[comm][python-phase5][tactile-scheduling]") {
    using namespace unified_detail;
    const auto now = runtime::Deadline{1s};
    const auto stage = python_next_tactile_stage(Model::kL6);
    REQUIRE_FALSE(python_stage_has_budget(now, now - 1us, stage));
    REQUIRE_FALSE(python_stage_has_budget(now, now + stage, stage));
    REQUIRE(python_stage_has_budget(now, now + stage + 1us, stage));
    REQUIRE(python_stage_has_budget(now, runtime::Deadline::max(), stage));
    REQUIRE_FALSE(python_stage_has_budget(now, runtime::Deadline::max(), python_next_tactile_stage(Model::kL30)));
}

TEST_CASE("O30 V25 yields after complete HOP metadata and selection transactions",
          "[comm][python-phase5][tactile-scheduling]") {
    for (const auto policy :
         {AngleFeedbackPolicy::kQueryOnly, AngleFeedbackPolicy::kCommandFeedbackOnly, AngleFeedbackPolicy::kAdaptive}) {
        auto hop = std::make_shared<o30_comm::HopEndpoint>();
        hop->seed_defaults();
        hop->put_ascii(o30::protocol::kMiProductInfo, 0x61, 8, "0.0.4");
        hop->put_ascii(o30::protocol::kMiProductInfo, 0xD9, 8, "left");
        hop->put(o30::protocol::kMiSensorInfo, std::vector<uint8_t>(0x73, 0));
        hop->put_ascii(o30::protocol::kMiSensorInfo, 0, 0x20, "TACTILE");
        hop->put_ascii(o30::protocol::kMiSensorInfo, 0x2C, 7, "Fn_U8_1");
        auto &info = hop->objects[{o30::protocol::kMiSensorInfo, false}];
        info[0x28] = 255;
        info[0x29] = info[0x2A] = 1;
        info[0x2B] = 7;
        info[0x6C] = info[0x6E] = info[0x6F] = info[0x70] = 1;
        hop->put(o30::protocol::kMiSensorCh1, {42});
        HandDeviceConfig config;
        config.model = Model::kO30;
        config.side = HandSide::kLeft;
        config.address = 1;
        config.o30_layout = O30Layout::kPythonV25;
        config.runtime_behavior = RuntimeBehavior::kPython;
        std::promise<void> entered, resume, incomplete;
        Message withheld{};
        auto release = resume.get_future().share();
        std::atomic<bool> gate{false};
        linkerbot::can::FakeTransport *raw = nullptr;
        HandDevice device(config, [&] {
            auto fake = std::make_unique<linkerbot::can::FakeTransport>();
            raw = fake.get();
            fake->on_request(1, [&, hop](const auto &request) {
                if (request.data[0] == o30::protocol::kMiSensorInfo && gate.exchange(false)) {
                    entered.set_value();
                    release.wait();
                    auto frames = hop->reply(request);
                    withheld = frames.back();
                    frames.pop_back();
                    incomplete.set_value();
                    return frames;
                }
                return hop->reply(request);
            });
            return fake;
        });
        device.open();
        raw->clear_sent();
        gate = true;
        auto force = std::async(std::launch::async, [&] { return device.get_force(3s); });
        const bool reached = entered.get_future().wait_for(1s) == std::future_status::ready;
        auto command = device.set_angles(std::vector<int32_t>(20, 20));
        device.start_polling({{TelemetrySemantic::kAngles, 1s, policy}});
        resume.set_value();
        const bool partial = incomplete.get_future().wait_for(1s) == std::future_status::ready;
        const auto before_completion = raw->sent();
        raw->inject(withheld);
        const auto sample = force.get();
        validation::complete(command);
        device.stop_polling();
        REQUIRE(reached);
        REQUIRE(partial);
        REQUIRE_FALSE(before_completion.empty());
        REQUIRE(std::none_of(before_completion.begin(), before_completion.end(),
                             [](const auto &frame) { return frame.data[0] == o30::protocol::kMiPosition; }));
        REQUIRE(sample.sensors.size() == 5);
        const auto sent = raw->sent();
        const auto selection = std::find_if(
            sent.begin(), sent.end(), [](const auto &frame) { return frame.data[0] == o30::protocol::kMiSensorInfo; });
        REQUIRE(selection != sent.end());
        REQUIRE(std::distance(selection, sent.end()) > 3);
        REQUIRE(selection[1].data[0] == o30::protocol::kMiPosition);
        if (policy == AngleFeedbackPolicy::kQueryOnly) {
            REQUIRE(selection[2].data[0] == o30::protocol::kMiPosition);
            REQUIRE(selection[3].data[0] == o30::protocol::kMiSensorInfo);
        } else {
            REQUIRE(selection[2].data[0] == o30::protocol::kMiSensorInfo);
        }
    }
}

TEST_CASE("Nested angle reads retain active demand when command feedback cancels a poll",
          "[comm][python-phase5][tactile-scheduling]") {
    unified_detail::ReadCoordinator<int> reads(3);
    std::promise<void> outer_entered, resume;
    auto release = resume.get_future().share();
    auto lane = std::make_shared<int>(42);
    std::atomic<int> nested{0};
    std::atomic<bool> same_lane{false};
    auto outer = std::async(std::launch::async, [&] {
        return reads.read_prepared(
            0, runtime::Clock::now() + 3s,
            [&](const std::shared_ptr<void> &lease) {
                outer_entered.set_value();
                release.wait();
                reads.cancel_pending_poll(1);
                same_lane = reads.serve_pending(1, lease);
                return 7;
            },
            [lane](const auto &) { return lane; });
    });
    const bool reached = outer_entered.get_future().wait_for(1s) == std::future_status::ready;
    reads.request_poll(
        1,
        [&](const std::shared_ptr<void> &lease) {
            if (lease == lane && !reads.claimed_by_poll()) {
                ++nested;
            }
            return 99;
        },
        [] { return true; });
    auto active =
        std::async(std::launch::async, [&] { return reads.read(1, runtime::Clock::now() + 3s, [] { return -1; }); });
    auto second =
        std::async(std::launch::async, [&] { return reads.read(1, runtime::Clock::now() + 3s, [] { return -2; }); });
    const auto deadline = runtime::Clock::now() + 1s;
    while (reads.waiter_count() != 3 && runtime::Clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool joined = reads.waiter_count() == 3;
    resume.set_value();
    const auto outer_value = outer.get();
    const auto active_value = active.get();
    REQUIRE(reached);
    REQUIRE(joined);
    REQUIRE(outer_value == 7);
    REQUIRE(active_value == 99);
    REQUIRE(second.get() == 99);
    REQUIRE(same_lane);
    REQUIRE(nested == 1);
}

TEST_CASE("Python stage sampling policies preserve exact deadlines and plan windows",
          "[comm][python-phase5][tactile-scheduling]") {
    using unified_detail::PythonState;
    for (const auto policy :
         {AngleFeedbackPolicy::kQueryOnly, AngleFeedbackPolicy::kCommandFeedbackOnly, AngleFeedbackPolicy::kAdaptive}) {
        auto now = runtime::Deadline{1s};
        PythonState state(Device::configuration(Model::kL6, RuntimeBehavior::kPython), [&] { return now; });
        const PollingPlan plan{{TelemetrySemantic::kAngles, 10ms, policy}};
        REQUIRE(state.replace(plan));
        const auto deadline = state.next_poll();
        runtime::Sample feedback{42, true, now, 1, 1, 1, protocol::Origin::command_feedback};
        state.publish("angle", feedback);
        const auto first = state.next_poll();
        REQUIRE(first == (policy == AngleFeedbackPolicy::kQueryOnly ? deadline : std::optional{now + 10ms}));
        REQUIRE_FALSE(state.replace(plan));
        REQUIRE(state.next_poll() == first);
        HandHealth health;
        state.project_health(health);
        REQUIRE(angle_metrics(health).command_feedback_samples == 1);
        REQUIRE(angle_metrics(health).feedback_samples == (policy == AngleFeedbackPolicy::kQueryOnly ? 0 : 1));
        now += 10ms; // Equal to expiry: feedback no longer defers the due sample.
        const auto ticket = state.take_poll(TelemetrySemantic::kAngles);
        REQUIRE(ticket.has_value() == (policy != AngleFeedbackPolicy::kCommandFeedbackOnly));
        REQUIRE(state.next_poll() == std::optional{now + 10ms});
        const PollingPlan replacement{{TelemetrySemantic::kAngles, 20ms, AngleFeedbackPolicy::kQueryOnly}};
        REQUIRE(state.replace(replacement));
        if (ticket) {
            REQUIRE_FALSE(state.poll_needed(*ticket));
        }
        state.project_health(health);
        REQUIRE(angle_metrics(health).feedback_samples == 0);
        REQUIRE(state.next_poll() == std::optional{now});
        state.stop_polling();
        REQUIRE_FALSE(state.next_poll());
    }
    auto caps = declared_capabilities(Model::kL6);
    for (auto &command : caps.commands) {
        if (command.semantic == CommandSemantic::kAngles) {
            command.returns_current_state = false;
        }
    }
    PythonState state(Device::configuration(Model::kL6, RuntimeBehavior::kPython));
    state.configure(caps);
    state.replace({{TelemetrySemantic::kAngles, 1s, AngleFeedbackPolicy::kAdaptive}});
    const auto due = state.next_poll();
    state.publish("angle", {42, true, runtime::Clock::now(), 1, 1, 1, protocol::Origin::command_feedback});
    REQUIRE(state.next_poll() == due);
    REQUIRE(state.view().samples.empty());
    HandHealth health;
    state.project_health(health);
    REQUIRE(angle_metrics(health).feedback_samples == 0);
}

TEST_CASE("Nested angle feedback failures retain real dispatch and count only successful outer work",
          "[comm][python-phase5][tactile-scheduling]") {
    for (int failure : {1, 2}) {
        Device device(Model::kL6);
        const auto previous_angle = test_support::require_value(device.hand.get_cached_snapshot().angles).meta;
        const auto before = device.hand.get_health();
        device.script->feedback_mode = failure;
        device.script->gate = true;
        auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
        const auto reached = device.script->first_finger.get_future().wait_for(1s);
        auto command = device.hand.set_angles(std::vector<int32_t>(6, 20));
        device.hand.start_polling({{TelemetrySemantic::kAngles, 1s, AngleFeedbackPolicy::kCommandFeedbackOnly}});
        device.script->resume.set_value();
        const auto result = force.get();
        REQUIRE(command.wait_for(1s));
        REQUIRE(reached == std::future_status::ready);
        REQUIRE(result.tactile.size() == 5);
        const auto after = device.hand.get_health();
        REQUIRE(after.transaction_latency.count == before.transaction_latency.count + 1);

        REQUIRE(after.recent_error);
        REQUIRE(after.recent_error->context.channel == "angle");
        REQUIRE(command.stage() == CommandStage::kDispatched);
        REQUIRE(command.sent_frames() == 1);
        REQUIRE(command.send_results().size() == 1);
        REQUIRE(test_support::require_value(device.hand.get_cached_snapshot().angles).meta.sequence ==
                previous_angle.sequence);
        REQUIRE(angle_metrics(after).feedback_samples == 0);
        REQUIRE(angle_metrics(after).completed_reads == 0);
        device.hand.stop_polling();
    }
}

TEST_CASE("Nested angle physical send failure aborts the tactile batch without replacing its cache",
          "[comm][python-phase5][tactile-scheduling]") {
    Device device(Model::kL6);
    const auto previous = device.hand.get_force(3s);
    const auto before = device.hand.get_health();
    device.script->gate = true;
    device.script->feedback_mode = 3;
    auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
    const auto reached = device.script->first_finger.get_future().wait_for(1s);
    const auto fingers = device.script->finger_requests.load();
    auto command = device.hand.set_angles(std::vector<int32_t>(6, 20));
    device.script->resume.set_value();
    REQUIRE_THROWS_AS(force.get(), TransportError);
    REQUIRE(reached == std::future_status::ready);
    REQUIRE(command.wait_for(1s));
    REQUIRE(command.stage() == CommandStage::kFailed);
    REQUIRE(command.send_results().size() == 1);
    REQUIRE(device.script->finger_requests == fingers);
    REQUIRE(test_support::require_value(device.hand.get_cached_snapshot().force).meta.sequence ==
            previous.meta.sequence);
    REQUIRE(device.hand.get_health().transaction_latency.count == before.transaction_latency.count);
}

TEST_CASE("Each tactile stage retains its own response window after an angle query times out",
          "[comm][python-phase5][tactile-scheduling]") {
    auto config = Device::configuration(Model::kL6, RuntimeBehavior::kPython);
    config.response_timeout = 40ms;
    Device device(config);
    device.script->gate = true;
    device.script->hold_angle = true;
    auto force = std::async(std::launch::async, [&] { return device.hand.get_force(2s); });
    const auto reached = device.script->first_finger.get_future().wait_for(1s);
    device.hand.start_polling({{TelemetrySemantic::kAngles, 1s, AngleFeedbackPolicy::kQueryOnly}});
    device.script->resume.set_value();
    const auto result = force.get();
    const auto health = device.hand.get_health();
    device.hand.stop_polling();
    REQUIRE(reached == std::future_status::ready);
    REQUIRE(result.tactile.size() == 5);
    REQUIRE(device.script->angle_queries == 1);
    REQUIRE(angle_metrics(health).timeouts == 1);
    REQUIRE(angle_metrics(health).completed_reads == 0);
    REQUIRE(health.recent_error);
    REQUIRE(health.recent_error->code == ErrorCode::kTimeout);
    REQUIRE(health.recent_error->context.channel == "angle");
}

TEST_CASE("Close during tactile or nested angle responses fences later stages and supports reopen",
          "[comm][python-phase5][tactile-scheduling]") {
    for (const bool nested : {false, true}) {
        auto config = Device::configuration(Model::kL6, RuntimeBehavior::kPython);
        config.response_timeout = 500ms;
        Device device(config);
        const auto previous = device.hand.get_force(3s);
        const auto generation = previous.meta.connection_generation;
        if (nested) {
            device.script->gate = true;
            device.script->hold_angle = true;
        } else {
            device.script->held_finger = 0xB1;
        }
        const auto count = device.script->finger_requests.load();
        auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
        if (nested) {
            const auto entered = device.script->first_finger.get_future().wait_for(1s);
            device.hand.start_polling({{TelemetrySemantic::kAngles, 2s, AngleFeedbackPolicy::kQueryOnly}});
            device.script->resume.set_value();
            REQUIRE(entered == std::future_status::ready);
            REQUIRE(observed([&] { return device.script->angle_queries > 0; }));
        } else {
            REQUIRE(observed([&] { return device.script->finger_requests > count; }));
        }
        const auto before_close = device.script->finger_requests.load();
        auto pending = device.hand.set_angles(std::vector<int32_t>(6, 30));
        device.hand.close();
        REQUIRE(closed_read(force));
        REQUIRE(pending.wait_for(1s));
        REQUIRE(pending.stage() == CommandStage::kFailed);
        REQUIRE(device.script->finger_requests == before_close);
        const auto closed = device.hand.get_cached_snapshot();
        // Closing exposes unavailable metadata; retained data must not become valid.
        REQUIRE(test_support::require_value(closed.force).meta.sequence == 0);
        REQUIRE_FALSE(test_support::require_value(closed.force).meta.valid);
        device.script->held_finger = 0;
        device.script->hold_angle = false;
        device.hand.open();
        REQUIRE(device.hand.get_cached_snapshot().connection_generation > generation);
        REQUIRE(device.script->angle_writes == 0); // No pending motion replay.
        REQUIRE(device.hand.get_force(2s).tactile.size() == 5);
    }
}

TEST_CASE("Close drains every frame of a claimed nested angle batch without completing partial tactile data",
          "[comm][python-phase5][tactile-scheduling]") {
    Device device(Model::kL20);
    device.script->held_finger = 0xB1;
    auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
    REQUIRE(observed([&] { return device.script->finger_requests == 1; }));
    auto command = device.hand.set_angles(std::vector<int32_t>(16, 30));
    device.transport->arm_tx_gate();
    inject_finger(device, 0xB1);
    const bool claimed = device.transport->wait_until_tx_entered(1s);
    auto close = std::async(std::launch::async, [&] { device.hand.close(); });
    const bool closing = observed([&] { return device.hand.get_health().lifecycle_state == LifecycleState::kClosing; });
    device.transport->release_tx_gate();
    close.get();
    REQUIRE(claimed);
    REQUIRE(closing);
    REQUIRE(closed_read(force));
    REQUIRE(command.wait_for(1s));
    REQUIRE(command.stage() == CommandStage::kDispatched);
    REQUIRE(command.sent_frames() == 5);
    const auto receipts = command.send_results();
    REQUIRE(receipts.size() == 1);
    REQUIRE(receipts.front().sent_frames == 5);
    REQUIRE(device.script->finger_requests == 2); // Includes the test's injected assembler input.
    REQUIRE_FALSE(test_support::require_value(device.hand.get_cached_snapshot().force).meta.valid);
}

TEST_CASE("Stopping a nested poll drains its physical query without ending tactile work",
          "[comm][python-phase5][tactile-scheduling]") {
    auto config = Device::configuration(Model::kL6, RuntimeBehavior::kPython);
    config.response_timeout = 500ms;
    Device device(config);
    device.script->gate = true;
    device.script->hold_angle = true;
    auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
    const auto entered = device.script->first_finger.get_future().wait_for(1s);
    device.hand.start_polling({{TelemetrySemantic::kAngles, 2s, AngleFeedbackPolicy::kQueryOnly}});
    device.script->resume.set_value();
    REQUIRE(entered == std::future_status::ready);
    REQUIRE(observed([&] { return device.script->angle_queries == 1; }));
    auto stop = std::async(std::launch::async, [&] { device.hand.stop_polling(); });
    const bool stopping = observed([&] { return !device.hand.get_health().polling_active; });
    const auto still_draining = stop.wait_for(20ms);
    inject_angles(device);
    stop.get();
    REQUIRE(stopping);
    REQUIRE(still_draining == std::future_status::timeout);
    REQUIRE(force.get().tactile.size() == 5);
    REQUIRE(device.script->angle_queries == 1);
    REQUIRE_FALSE(device.hand.get_health().polling_active);
    device.script->hold_angle = false;
    device.hand.recover_for(2s);
    REQUIRE_FALSE(device.hand.get_health().polling_active);
}

TEST_CASE("Nested polling has a bounded stop and plan replacement cannot count an old query",
          "[comm][python-phase5][tactile-scheduling]") {
    auto config = Device::configuration(Model::kL6, RuntimeBehavior::kPython);
    config.response_timeout = 500ms;
    config.shutdown_timeout = 30ms;
    Device device(config);
    device.script->gate = true;
    device.script->hold_angle = true;
    auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
    const auto entered = device.script->first_finger.get_future().wait_for(1s);
    device.hand.start_polling({{TelemetrySemantic::kAngles, 2s, AngleFeedbackPolicy::kQueryOnly}});
    device.script->resume.set_value();
    REQUIRE(entered == std::future_status::ready);
    REQUIRE(observed([&] { return device.script->angle_queries == 1; }));
    REQUIRE_THROWS_AS(device.hand.stop_polling(), TimeoutError);
    REQUIRE(device.hand.is_open());
    // State-level revision fencing also rejects completions from an old plan.
    inject_angles(device);
    REQUIRE(force.get().tactile.size() == 5);
    REQUIRE_NOTHROW(device.hand.stop_polling());
    REQUIRE_FALSE(device.hand.get_health().polling_active);
    unified_detail::PythonState state(config);
    state.replace({{TelemetrySemantic::kAngles, 1s, AngleFeedbackPolicy::kQueryOnly}});
    const auto old = state.poll_revision();
    state.replace({{TelemetrySemantic::kAngles, 2s, AngleFeedbackPolicy::kQueryOnly}});
    state.read_succeeded(TelemetrySemantic::kAngles, true, old);
    state.read_failed(TelemetrySemantic::kAngles, true, ResponseTimeoutError("old query"), true, old);
    HandHealth health;
    state.project_health(health);
    REQUIRE(angle_metrics(health).completed_reads == 0);
    REQUIRE(angle_metrics(health).feedback_samples == 0);
    REQUIRE(angle_metrics(health).timeouts == 0);
}

TEST_CASE("Caller tactile timeout leaves the shared staged operation and logical completion intact",
          "[comm][python-phase5][tactile-scheduling]") {
    Device device(Model::kL6);
    device.script->held_finger = 0xB1;
    const auto before = device.hand.get_health().transaction_latency.count;
    auto short_wait = std::async(std::launch::async, [&] { return device.hand.get_force(20ms); });
    REQUIRE(observed([&] { return device.script->finger_requests == 1; }));
    REQUIRE_THROWS_AS(short_wait.get(), TimeoutError);
    auto command = device.hand.set_angles(std::vector<int32_t>(6, 20));
    inject_finger(device, 0xB1);
    validation::complete(command);
    REQUIRE(observed([&] { return test_support::require_value(device.hand.get_cached_snapshot().force).meta.valid; }));
    REQUIRE(test_support::require_value(device.hand.get_cached_snapshot().force).tactile.size() == 5);
    REQUIRE(device.hand.get_health().transaction_latency.count == before + 2);
    REQUIRE(device.script->angle_writes == 1);
}

TEST_CASE("Continuously replaced angle targets cannot bypass the boundary cap under mixed load",
          "[comm][python-phase5][tactile-scheduling]") {
    for (const bool mixed : {false, true}) {
        auto config = Device::configuration(Model::kL6, RuntimeBehavior::kPython);
        config.command_rate_hz = 1000000;
        Device device(config);
        device.script->gate = true;
        std::vector<CommandHandle> commands;
        device.script->on_angle = [&](const Message &request) {
            if (request.dlc > 1 && device.script->angle_writes < 20) {
                commands.push_back(device.hand.set_angles(std::vector<int32_t>(6, 20)));
            }
        };
        auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
        const bool entered = device.script->first_finger.get_future().wait_for(1s) == std::future_status::ready;
        auto initial = device.hand.set_angles(std::vector<int32_t>(6, 10));
        if (mixed) {
            device.script->slow_angles = true;
            device.hand.start_polling({{TelemetrySemantic::kAngles, 1ms, AngleFeedbackPolicy::kQueryOnly}});
        }
        device.script->resume.set_value();
        REQUIRE(force.get().tactile.size() == 5);
        device.hand.stop_polling();
        const auto sent = device.transport->sent();
        // Close drains any final queued target before inspecting callback-owned data.
        device.hand.close();
        REQUIRE(entered);
        REQUIRE(initial.wait_for(1s));
        REQUIRE(device.script->angle_writes >= 4);
        for (uint8_t opcode = 0xB1; opcode < 0xB5; ++opcode) {
            const auto first =
                std::find_if(sent.begin(), sent.end(), [=](const auto &m) { return m.data[0] == opcode; });
            const auto next = std::find_if(first, sent.end(), [=](const auto &m) { return m.data[0] == opcode + 1; });
            REQUIRE(first != sent.end());
            REQUIRE(next != sent.end());
            REQUIRE(std::distance(first, next) == 5);
        }
        if (mixed) {
            REQUIRE(device.script->angle_queries >= 4);
        }
        for (const auto &command : commands) {
            REQUIRE(command.wait_for(1s));
        }
    }
}

TEST_CASE("A nested tactile angle wait releases the shared Session sender and isolates peer close",
          "[comm][python-phase5][tactile-scheduling]") {
    auto fake = std::make_unique<linkerbot::can::FakeTransport>();
    auto *raw = fake.get();
    auto script = std::make_shared<Script>(Model::kL6);
    fake->on_request(0x28, [script](const auto &request) { return script->reply(request); });
    fake->on_request(0x27, [](const auto &request) {
        auto replies = validation::l6_reply(request);
        if (request.data[0] == 0xC0) {
            const size_t index = std::string("LHL6-1-123-L").size() - 1;
            replies.at(index / 6).data[2 + index % 6] = 'R';
        }
        return replies;
    });
    auto session = linkerbot::transport::Session::own(std::move(fake));
    auto config = Device::configuration(Model::kL6, RuntimeBehavior::kPython);
    config.response_timeout = 500ms;
    HandDevice left(config, session);
    config.side = HandSide::kRight;
    HandDevice right(config, session);
    left.open();
    right.open();
    script->angle_queries = 0;
    script->held_finger = 0xB1;
    script->hold_angle = true;
    const auto count = script->finger_requests.load();
    auto force = std::async(std::launch::async, [&] { return left.get_force(3s); });
    REQUIRE(observed([&] { return script->finger_requests > count; }));
    left.start_polling({{TelemetrySemantic::kAngles, 2s, AngleFeedbackPolicy::kQueryOnly}});
    Message request{};
    request.id = 0x28;
    request.dlc = 2;
    request.data[0] = 0xB1;
    script->held_finger = 0;
    for (const auto &frame : script->reply(request)) {
        raw->inject(frame);
    }
    REQUIRE(observed([&] { return script->angle_queries == 1; }));
    const auto peer = right.get_angles(1s);
    const auto generation = peer.meta.connection_generation;
    const auto sequence = peer.meta.sequence;
    left.close();
    REQUIRE_THROWS_AS(force.get(), HandError);
    REQUIRE(right.is_open());
    REQUIRE(session->health().open);
    const auto next = right.get_angles(1s);
    REQUIRE(next.raw == std::vector<double>(6, 64));
    REQUIRE(next.meta.connection_generation == generation);
    REQUIRE(next.meta.sequence == sequence + 1);
    REQUIRE(right.get_health().consecutive_errors == 0);
    right.close();
    session->close();
}

TEST_CASE("L30 complete tactile frames do not introduce a Python yield contract",
          "[comm][python-phase5][tactile-scheduling]") {
    auto fake = std::make_unique<linkerbot::can::FakeTransport>();
    for (const auto finger : l30::kAllFingers) {
        fake->on_request(l30_comm::request_id(l30::protocol::kParentTactile, l30::finger_subcommand(finger),
                                              l30::protocol::kAccessRead),
                         l30_comm::tactile_handler(l30_comm::tactile_ramp()));
    }
    l30::L30 hand(std::move(fake), l30_comm::kNodeId, l30_comm::kHostId, false);
    unsigned boundaries = 0;
    unified_detail::RuntimeAccess::with_stage_boundaries(
        *hand.runtime(), [&] { ++boundaries; }, [&] { (void)hand.force_sensor.get_blocking(500ms); });
    REQUIRE(boundaries == 0);
    REQUIRE(hand.force_sensor.get_snapshot().has_value());
}

TEST_CASE("Replacing a plan during a nested query fences its counters without losing the tactile operation",
          "[comm][python-phase5][tactile-scheduling]") {
    auto config = Device::configuration(Model::kL6, RuntimeBehavior::kPython);
    config.response_timeout = 500ms;
    Device device(config);
    device.script->gate = true;
    device.script->hold_angle = true;
    auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
    const auto entered = device.script->first_finger.get_future().wait_for(1s);
    device.hand.start_polling({{TelemetrySemantic::kAngles, 2s, AngleFeedbackPolicy::kQueryOnly}});
    device.script->resume.set_value();
    REQUIRE(entered == std::future_status::ready);
    REQUIRE(observed([&] { return device.script->angle_queries == 1; }));
    device.hand.start_polling({{TelemetrySemantic::kAngles, 3s, AngleFeedbackPolicy::kCommandFeedbackOnly}});
    inject_angles(device);
    REQUIRE(force.get().tactile.size() == 5);
    const auto metrics = angle_metrics(device.hand.get_health());
    REQUIRE(metrics.feedback_policy == AngleFeedbackPolicy::kCommandFeedbackOnly);
    REQUIRE(metrics.completed_reads == 0);
    REQUIRE(metrics.feedback_samples == 0);
    REQUIRE(metrics.timeouts == 0);
    REQUIRE(device.script->angle_queries == 1);
    device.hand.stop_polling();
}

TEST_CASE("Recovery invalidates nested tactile work and resumes scheduling without replaying old motion",
          "[comm][python-phase5][tactile-scheduling]") {
    for (const bool fail_first : {false, true}) {
        auto config = Device::configuration(Model::kL6, RuntimeBehavior::kPython);
        config.response_timeout = 500ms;
        config.recovery_quiet = 5ms;
        Device device(config);
        const auto previous = device.hand.get_force(2s);
        device.script->gate = true;
        device.script->hold_angle = true;
        auto force = std::async(std::launch::async, [&] { return device.hand.get_force(3s); });
        const auto entered = device.script->first_finger.get_future().wait_for(1s);
        device.hand.start_polling({{TelemetrySemantic::kAngles, 2s, AngleFeedbackPolicy::kQueryOnly}});
        device.script->resume.set_value();
        REQUIRE(entered == std::future_status::ready);
        REQUIRE(observed([&] { return device.script->angle_queries == 1; }));
        auto old = device.hand.set_angles(std::vector<int32_t>(6, 40));
        // The old physical query still has no reply; only generation cancellation
        // can release it. New generation initialization can answer normally.
        device.script->hold_angle = false;
        if (fail_first) {
            device.script->bad_angles = true;
            REQUIRE_THROWS_AS(device.hand.recover_for(2s), HandError);
            REQUIRE(device.script->angle_writes == 0);
            device.script->bad_angles = false;
        }
        device.hand.recover_for(2s);
        REQUIRE_THROWS_AS(force.get(), HandError);
        REQUIRE(old.wait_for(1s));
        REQUIRE(old.stage() == CommandStage::kFailed);
        REQUIRE(device.script->angle_writes == 0);
        REQUIRE(device.hand.get_cached_snapshot().connection_generation > previous.meta.connection_generation);
        device.hand.stop_polling();
        REQUIRE(device.hand.get_force(2s).tactile.size() == 5);
        auto fresh = device.hand.set_angles(std::vector<int32_t>(6, 20));
        validation::complete(fresh);
        REQUIRE(device.script->angle_writes == 1);
    }
}

TEST_CASE("Nested query transport failure terminates its enclosing tactile operation",
          "[comm][python-phase5][tactile-scheduling]") {
    Device device(Model::kL6);
    const auto previous = device.hand.get_force(2s);
    (void)device.hand.get_cached_snapshot(); // Retain the complete snapshot before facade fault invalidation.
    device.script->gate = true;
    device.script->on_angle = [](const Message &) {
        throw linkerbot::can::TransportIOError("nested query send failed");
    };
    auto force = std::async(std::launch::async, [&] { return device.hand.get_force(2s); });
    const auto entered = device.script->first_finger.get_future().wait_for(1s);
    device.hand.start_polling({{TelemetrySemantic::kAngles, 2s, AngleFeedbackPolicy::kQueryOnly}});
    device.script->resume.set_value();
    REQUIRE(entered == std::future_status::ready);
    REQUIRE_THROWS_AS(force.get(), TransportError);
    device.hand.stop_polling();
    const auto cached = test_support::require_value(device.hand.get_cached_snapshot().force);
    REQUIRE(cached.raw == previous.raw);
    REQUIRE_FALSE(cached.meta.valid);
    REQUIRE(device.script->finger_requests == 6); // Five cached fingers, then only the failed round's first finger.
    REQUIRE(device.hand.get_health().recent_error->context.channel == "angle");
}

TEST_CASE("Repeated tactile traffic allows other reads and continuously renewed motion to progress",
          "[comm][python-phase5][tactile-scheduling]") {
    auto config = Device::configuration(Model::kL6, RuntimeBehavior::kPython);
    config.command_rate_hz = 1000000;
    Device device(config);
    device.script->gate = true;
    std::vector<CommandHandle> renewed;
    device.script->on_angle = [&](const Message &request) {
        if (request.dlc > 1 && device.script->angle_writes < 20) {
            renewed.push_back(device.hand.set_angles(std::vector<int32_t>(6, 20)));
        }
    };
    auto tactile = std::async(std::launch::async, [&] {
        uint64_t sequence = 0;
        for (unsigned i = 0; i < 12; ++i) {
            const auto sample = device.hand.get_force(2s);
            if (sample.tactile.size() != 5 || sample.meta.sequence <= sequence) {
                return false;
            }
            sequence = sample.meta.sequence;
        }
        return true;
    });
    const auto entered = device.script->first_finger.get_future().wait_for(1s);
    auto other = std::async(std::launch::async, [&] {
        uint64_t sequence = 0;
        for (unsigned i = 0; i < 12; ++i) {
            const auto sample = device.hand.get_currents(2s);
            if (sample.meta.sequence <= sequence) {
                return false;
            }
            sequence = sample.meta.sequence;
        }
        return true;
    });
    auto initial = device.hand.set_angles(std::vector<int32_t>(6, 10));
    device.script->resume.set_value();
    REQUIRE(entered == std::future_status::ready);
    REQUIRE(tactile.get());
    REQUIRE(other.get());
    REQUIRE(observed([&] { return device.script->angle_writes == 20; }));
    device.hand.close();
    REQUIRE(initial.wait_for(1s));
    REQUIRE(renewed.size() == 19);
    for (const auto &handle : renewed) {
        REQUIRE(handle.wait_for(1s));
        REQUIRE(handle.stage() == CommandStage::kDispatched);
    }
}

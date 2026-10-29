#pragma once
#include "legacy_regressions.hpp"
#include <linkerbot/hand/l20lite/current_manager.hpp>
#include <linkerbot/hand/l20lite/force_sensor_manager.hpp>
#include <linkerbot/hand/l20lite/version_manager.hpp>
#include <linkerbot/hand/o6/angle_manager.hpp>
#include <linkerbot/hand/o6/current_manager.hpp>
#include <linkerbot/hand/o6/force_sensor_manager.hpp>
#include <linkerbot/hand/o6/version_manager.hpp>

namespace linkerbot::hand::o6 {
    struct ManagerTestAccess {
        template <class M, class F> static void sink(M &m, F f) { m.set_event_sink(std::move(f)); }
        static void poll(ForceSensorManager &m) { m.send_sense_request(); }
        static auto &gate(CurrentManager &m) { return m.channel_; }
        static auto &gate(VersionManager &m) { return m.channel_; }
        static auto &gate(ForceSensorManager &m) { return m.channel_; }
        static auto &gate(SingleForceSensorManager &m) { return m.channel_; }
        template <class M> static void cancel(M &m) { m.cancel_pending(); }
        static auto first_fingers(ForceSensorManager &m) {
            auto it = m.fingers_.begin();
            auto *first = it->second.get();
            auto *second = (++it)->second.get();
            return std::make_pair(first, second);
        }
        static auto command(SingleForceSensorManager &m) { return m.command_prefix_; }
    };
}
namespace linkerbot::hand::l20lite {
    struct ManagerTestAccess {
        template <class M, class F> static void sink(M &m, F f) { m.set_event_sink(std::move(f)); }
        static void poll(ForceSensorManager &m) { m.send_sense_request(); }
        static auto &gate(CurrentManager &m) { return m.channel_; }
        static auto &gate(VersionManager &m) { return m.channel_; }
        static auto &gate(ForceSensorManager &m) { return m.channel_; }
        static auto &gate(SingleForceSensorManager &m) { return m.channel_; }
        template <class M> static void cancel(M &m) { m.cancel_pending(); }
        static auto first_fingers(ForceSensorManager &m) {
            auto it = m.fingers_.begin();
            auto *first = it->second.get();
            auto *second = (++it)->second.get();
            return std::make_pair(first, second);
        }
        static auto command(SingleForceSensorManager &m) { return m.command_prefix_; }
    };
}

namespace classic_regression {
    template <class Manager, class Access> void repeated_stop(int count, int bytes) {
        bool concurrent_start = false;
        SECTION("two external stops both wait for the callback") {}
        SECTION("restart waits for stop and preserves the subscription") { concurrent_start = true; }
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        raw->on_request(0x28, [=](const Message &r) { return rows(r, count, bytes); });
        linkerbot::can::CANDispatcher dispatcher(std::move(transport));
        Manager manager(0x28, dispatcher, 0xB1);
        manager.start();
        std::promise<void> entered, attempt, checked, release;
        auto attempted = attempt.get_future().share(), released = release.get_future().share();
        std::atomic<bool> start_rejected{false}, stop_rejected{false};
        Access::sink(manager, [&](const auto &) {
            entered.set_value();
            attempted.wait();
            try {
                manager.start();
            } catch (const linkerbot::hand::LifecycleError &) {
                start_rejected = true;
            }
            try {
                manager.stop();
            } catch (const linkerbot::hand::LifecycleError &) {
                stop_rejected = true;
            }
            checked.set_value();
            released.wait();
        });
        auto query = std::async(std::launch::async, [&] {
            try {
                (void)manager.get_blocking(500ms);
            } catch (...) {
            }
        });
        const auto reached = entered.get_future().wait_for(500ms);
        const auto query_finished = query.wait_for(500ms);
        auto stop1 = std::async(std::launch::async, [&] { manager.stop(); });
        const auto first_wait = stop1.wait_for(20ms);
        auto stop2 = std::async(std::launch::async, [&] {
            if (concurrent_start) {
                manager.start();
            } else {
                manager.stop();
            }
        });
        const auto second_wait = stop2.wait_for(20ms);
        attempt.set_value();
        const auto callback_finished = checked.get_future().wait_for(500ms);
        release.set_value();
        stop1.get();
        stop2.get();
        query.get();
        REQUIRE(reached == std::future_status::ready);
        REQUIRE(query_finished == std::future_status::ready);
        REQUIRE(first_wait == std::future_status::timeout);
        REQUIRE(second_wait == std::future_status::timeout);
        REQUIRE(callback_finished == std::future_status::ready);
        REQUIRE(start_rejected);
        REQUIRE(stop_rejected);
        Access::sink(manager, nullptr);
        manager.start();
        REQUIRE(manager.get_blocking(500ms).values[0][0] == 42);
        manager.stop();
    }

    // Both snapshots and events must retain exactly the last complete five-finger
    // operation. Updating a single finger never creates another whole-hand batch.
    template <class Manager, class Access>
    void full_force_batches(int count, int bytes, bool asynchronous_batch_polling = true) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        linkerbot::can::CANDispatcher dispatcher(std::move(transport));
        Manager manager(0x28, dispatcher);
        manager.start();
        std::atomic<int> fill{11}, missing{0}, events{0};
        std::promise<void> initial_event;
        auto initial_event_ready = initial_event.get_future().share();
        Access::sink(manager, [&](const auto &) {
            if (++events == 1) {
                initial_event.set_value();
            }
        });
        raw->on_request(0x28, [&](const Message &request) {
            if (request.data[0] == missing) {
                return std::vector<Message>{};
            }
            auto response = rows(request, count, bytes);
            for (auto &r : response) {
                for (int col = 0; col < bytes; ++col) {
                    r.data[2 + col] = fill;
                }
            }
            return response;
        });
        SECTION("blocking second batch timeout retains complete first batch") {
            REQUIRE(manager.get_blocking(500ms).pinky.values[0][0] == 11);
            fill = 22;
            missing = 0xB5;
            REQUIRE_THROWS(manager.get_blocking(40ms));
            REQUIRE(manager.get_finger("thumb").get_snapshot()->values[0][0] == 22);
        }
        SECTION("individual reads do not synthesize a whole-hand snapshot or event") {
            for (const auto *name : {"thumb", "index", "middle", "ring", "pinky"}) {
                REQUIRE(manager.get_finger(name).get_blocking(500ms).values[0][0] == 11);
            }
            REQUIRE_FALSE(manager.get_snapshot());
            REQUIRE(events == 0);
            (void)manager.get_blocking(500ms);
            fill = 33;
            REQUIRE(manager.get_finger("thumb").get_blocking(500ms).values[0][0] == 33);
        }
        SECTION("polling sends asynchronously and incomplete second batch retains first") {
            Access::poll(manager);
            REQUIRE(initial_event_ready.wait_for(500ms) == std::future_status::ready);
            fill = 22;
            missing = 0xB5;
            const auto start = std::chrono::steady_clock::now();
            if (asynchronous_batch_polling) {
                Access::poll(manager);
            } else {
                REQUIRE_THROWS(Access::poll(manager));
            }
            REQUIRE(std::chrono::steady_clock::now() - start < 150ms);
            REQUIRE(raw->sent().size() == 10);
            // Admission waits until the asynchronous batch expires. Its missing
            // finger then remains quarantined and cannot complete a replacement.
            REQUIRE_THROWS(manager.get_blocking(500ms));
        }
        SECTION("stop cancels a retained asynchronous batch and start preserves quarantine") {
            (void)manager.get_blocking(500ms);
            if (asynchronous_batch_polling) {
                fill = 22;
                missing = 0xB5;
                Access::poll(manager);
                const auto start = std::chrono::steady_clock::now();
                manager.stop();
                REQUIRE(std::chrono::steady_clock::now() - start < 200ms);
                manager.start();
                REQUIRE_THROWS(manager.get_blocking(500ms));
            }
        }
        const auto snapshot = manager.get_snapshot();
        REQUIRE(snapshot.has_value());
        REQUIRE(snapshot->thumb.values[0][0] == 11);
        REQUIRE(snapshot->index.values[0][0] == 11);
        REQUIRE(snapshot->middle.values[0][0] == 11);
        REQUIRE(snapshot->ring.values[0][0] == 11);
        REQUIRE(snapshot->pinky.values[0][0] == 11);
        REQUIRE(initial_event_ready.wait_for(500ms) == std::future_status::ready);
        REQUIRE(events == 1);
    }

    template <class Manager> void single_during_batch(int count, int bytes, bool sends_batch = false) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        linkerbot::can::CANDispatcher dispatcher(std::move(transport));
        Manager manager(0x28, dispatcher);
        manager.start();
        std::promise<void> middle_requested, batch_sent;
        raw->on_request(0x28, [&](const Message &r) {
            if (r.data[0] == 0xB3) {
                middle_requested.set_value();
                return std::vector<Message>{};
            }
            if (r.data[0] == 0xB5) {
                batch_sent.set_value();
            }
            return rows(r, count, bytes);
        });
        auto full = std::async(std::launch::async, [&] { return manager.get_blocking(500ms); });
        REQUIRE(middle_requested.get_future().wait_for(300ms) == std::future_status::ready);
        if (sends_batch) {
            REQUIRE(batch_sent.get_future().wait_for(300ms) == std::future_status::ready);
        }
        const auto sent = raw->sent().size();
        REQUIRE_THROWS(manager.get_finger("thumb").get_blocking(20ms));
        REQUIRE(raw->sent().size() == sent);
        Message request;
        request.id = 0x28;
        request.data[0] = 0xB3;
        for (const auto &r : rows(request, count, bytes)) {
            raw->inject(r);
        }
        REQUIRE(full.get().pinky.values[0][0] == 42);
        REQUIRE(manager.get_snapshot()->thumb.values[0][0] == 42);
    }
}

namespace linkerbot::hand::runtime {
    struct ChannelTestAccess {
        template <class T> static auto pin(Channel<T> &channel, size_t expected) {
            auto state = channel.calls_.state_;
            std::unique_lock lock(state->mutex);
            const bool entered =
                state->cv.wait_for(lock, std::chrono::milliseconds(500), [&] { return state->calls == expected; });
            return std::make_pair(entered, std::move(lock));
        }
    };
}
namespace linkerbot::hand::classic_detail {
    // Pin only guard teardown, after real API callers have entered admission. This
    // makes an omitted drain observable without relying on scheduler luck or UAF.
    struct AdmissionTestAccess {
        template <class T> static auto pin(runtime::Channel<T> &channel, size_t expected) {
            return runtime::ChannelTestAccess::pin(channel, expected);
        }
        template <class Gate> static auto pin(Gate &gate, size_t expected) {
            auto state = gate.calls_.state_;
            std::unique_lock lock(state->mutex);
            const bool entered =
                state->cv.wait_for(lock, std::chrono::milliseconds(500), [&] { return state->calls == expected; });
            return std::make_pair(entered, std::move(lock));
        }
    };
}

namespace classic_regression {
    template <class Manager, class Access, class Query>
    void queued_destruction(Query query, size_t expected, size_t max_frames = 2) {
        bool destroy = true;
        SECTION("destructor drains active and queued public calls") {}
        SECTION("external stop drains active and queued public calls") { destroy = false; }
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        linkerbot::can::CANDispatcher dispatcher(std::move(transport));
        auto owner = std::make_unique<Manager>(0x28, dispatcher);
        auto *manager = owner.get();
        manager->start();
        std::promise<void> sent;
        std::atomic<bool> signalled{false};
        raw->on_request(0x28, [&](const Message &) {
            if (!signalled.exchange(true)) {
                sent.set_value();
            }
            return std::vector<Message>{};
        });
        const auto invoke = [=] {
            try {
                query(*manager);
                return false;
            } catch (const std::exception &) {
                return true;
            }
        };
        auto active = std::async(std::launch::async, invoke);
        REQUIRE(sent.get_future().wait_for(500ms) == std::future_status::ready);
        auto queued1 = std::async(std::launch::async, invoke);
        auto queued2 = std::async(std::launch::async, invoke);
        auto pinned = linkerbot::hand::classic_detail::AdmissionTestAccess::pin(Access::gate(*manager), expected);
        Access::cancel(*manager);
        std::promise<void> stopping;
        auto stopped = std::async(std::launch::async, [&] {
            stopping.set_value();
            if (destroy) {
                owner.reset();
            } else {
                manager->stop();
            }
        });
        stopping.get_future().wait();
        const auto barrier = stopped.wait_for(30ms);
        pinned.second.unlock();
        stopped.get();
        REQUIRE(pinned.first);
        REQUIRE(barrier == std::future_status::timeout);
        REQUIRE(active.get());
        REQUIRE(queued1.get());
        REQUIRE(queued2.get());
        REQUIRE(raw->sent().size() <= max_frames); // No second transaction may be sent.
    }

    template <class Hand, class Side, class Access> void cancel_all_before_angle_barrier(Side side, bool dual) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        Hand hand(side, std::move(transport), "fake", test_util::unique_mapping_path());
        std::promise<void> current_sent, callback_entered;
        std::atomic<bool> signalled{false};
        raw->on_request(0x28, [&](const Message &request) {
            if (request.data[0] != 1 && request.data[0] != 4) {
                if (!signalled.exchange(true)) {
                    current_sent.set_value();
                }
                return std::vector<Message>{};
            }
            Message reply;
            reply.id = 0x28;
            reply.data[0] = request.data[0];
            reply.dlc = dual && request.data[0] == 4 ? 5 : 7;
            return std::vector<Message>{reply};
        });
        auto current = std::async(std::launch::async, [&] {
                           try {
                               (void)hand.current.get_blocking(5s);
                               return false;
                           } catch (const std::exception &) {
                               return true;
                           }
                       }).share();
        REQUIRE(current_sent.get_future().wait_for(500ms) == std::future_status::ready);
        Access::sink(hand.angle, [&](const auto &) {
            callback_entered.set_value();
            current.wait();
        });
        auto angle = std::async(std::launch::async, [&] {
            try {
                (void)hand.angle.get_blocking(5s);
            } catch (const std::exception &) {
            }
        });
        const auto entered = callback_entered.get_future().wait_for(500ms);
        auto stopped = std::async(std::launch::async, [&] { hand.stop(); });
        const auto timely = stopped.wait_for(300ms);
        stopped.get();
        angle.get();
        REQUIRE(entered == std::future_status::ready);
        REQUIRE(timely == std::future_status::ready);
        REQUIRE(current.get());
    }

    template <class Manager, class Access> void cancel_all_fingers_before_barrier(int count, int bytes) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        linkerbot::can::CANDispatcher dispatcher(std::move(transport));
        Manager manager(0x28, dispatcher);
        manager.start();
        // Follow the actual manager iteration order, independently of unordered_map.
        const auto fingers = Access::first_fingers(manager);
        const auto first_command = Access::command(*fingers.first);
        const auto second_command = Access::command(*fingers.second);
        std::promise<void> begin_second, second_sent, callback_entered;
        auto begin = begin_second.get_future().share();
        raw->on_request(0x28, [&](const Message &request) {
            if (request.data[0] == second_command) {
                second_sent.set_value();
                return std::vector<Message>{};
            }
            return request.data[0] == first_command ? rows(request, count, bytes) : std::vector<Message>{};
        });
        auto second = std::async(std::launch::async, [&] {
                          begin.wait();
                          try {
                              (void)fingers.second->get_blocking(5s);
                              return false;
                          } catch (const std::exception &) {
                              return true;
                          }
                      }).share();
        Access::sink(*fingers.first, [&](const auto &) {
            callback_entered.set_value();
            second.wait();
        });
        auto first = std::async(std::launch::async, [&] { return fingers.first->get_blocking(5s); });
        const auto entered = callback_entered.get_future().wait_for(500ms);
        const auto first_ready = first.wait_for(500ms);
        begin_second.set_value();
        const auto sent = second_sent.get_future().wait_for(500ms);
        auto stopped = std::async(std::launch::async, [&] { manager.stop(); });
        const auto timely = stopped.wait_for(300ms);
        stopped.get();
        REQUIRE(entered == std::future_status::ready);
        REQUIRE(first_ready == std::future_status::ready);
        REQUIRE(sent == std::future_status::ready);
        REQUIRE(timely == std::future_status::ready);
        REQUIRE(second.get());
        REQUIRE(first.get().values[0][0] == 42);
    }
}

namespace classic_regression {
    template <class Manager, class Access> void stop_single_queued_on_shared_tactile_gate() {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        linkerbot::can::CANDispatcher dispatcher(std::move(transport));
        Manager manager(0x28, dispatcher);
        manager.start();
        auto &thumb = manager.get_finger("thumb");
        auto &index = manager.get_finger("index");
        std::promise<void> sent;
        raw->on_request(0x28, [&](const Message &) {
            sent.set_value();
            return std::vector<Message>{};
        });
        auto active = std::async(std::launch::async, [&] {
            try {
                (void)thumb.get_blocking(5s);
                return false;
            } catch (const std::exception &) {
                return true;
            }
        });
        REQUIRE(sent.get_future().wait_for(500ms) == std::future_status::ready);
        auto queued = std::async(std::launch::async, [&] {
            try {
                (void)index.get_blocking(5s);
                return false;
            } catch (const std::exception &) {
                return true;
            }
        });
        auto admitted = linkerbot::hand::classic_detail::AdmissionTestAccess::pin(Access::gate(index), 1);
        admitted.second.unlock();
        auto stopped = std::async(std::launch::async, [&] { index.stop(); });
        const auto timely = stopped.wait_for(300ms);
        const auto cancelled = queued.wait_for(300ms);
        const auto other_pending = active.wait_for(0ms);
        manager.stop();
        stopped.get();
        REQUIRE(admitted.first);
        REQUIRE(timely == std::future_status::ready);
        REQUIRE(cancelled == std::future_status::ready);
        REQUIRE(other_pending == std::future_status::timeout);
        REQUIRE(queued.get());
        REQUIRE(active.get());
        REQUIRE(raw->sent().size() == 1);
    }
}

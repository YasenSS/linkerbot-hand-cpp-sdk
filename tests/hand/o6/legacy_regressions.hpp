#pragma once
#include "hand/support/temp_path.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <future>
#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/errors.hpp>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace classic_regression {
    using namespace std::chrono_literals;
    using linkerbot::can::FakeTransport;
    using linkerbot::can::Message;

    inline std::vector<Message> rows(const Message &request, int count, int bytes) {
        std::vector<Message> result;
        for (int row = 0; row < count; ++row) {
            Message reply;
            reply.id = request.id;
            reply.dlc = static_cast<uint8_t>(bytes + 2);
            reply.data[0] = request.data[0];
            reply.data[1] = static_cast<uint8_t>(row << 4);
            for (int col = 0; col < bytes; ++col) {
                reply.data[col + 2] = 42;
            }
            result.push_back(reply);
        }
        return result;
    }
    template <class Manager> void force_windows(int count, int bytes) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        linkerbot::can::CANDispatcher dispatcher(std::move(transport));
        Manager manager(0x28, dispatcher, 0xB1);
        manager.start();
        SECTION("unsolicited complete rows never open an implicit transaction") {
            std::promise<void> delivered;
            int received = 0;
            const auto tail = dispatcher.subscribe([&](const Message &) {
                if (++received == count) {
                    delivered.set_value();
                }
            });
            Message request;
            request.id = 0x28;
            request.data[0] = 0xB1;
            for (const auto &reply : rows(request, count, bytes)) {
                raw->inject(reply);
            }
            REQUIRE(delivered.get_future().wait_for(500ms) == std::future_status::ready);
            REQUIRE_FALSE(manager.get_snapshot());
            dispatcher.unsubscribe(tail);
        }
        SECTION("identical duplicate including row zero is idempotent") {
            raw->on_request(0x28, [=](const Message &request) {
                auto replies = rows(request, count, bytes);
                replies.insert(replies.begin() + 2, replies[0]);
                return replies;
            });
            const auto data = manager.get_blocking(200ms);
            REQUIRE(data.values[0][0] == 42);
        }
        SECTION("conflicting duplicate quarantines and never publishes") {
            raw->on_request(0x28, [=](const Message &request) {
                auto replies = rows(request, count, bytes);
                auto conflict = replies[1];
                conflict.data[2] = 99;
                replies.insert(replies.begin() + 2, conflict);
                return replies;
            });
            REQUIRE_THROWS(manager.get_blocking(200ms));
            REQUIRE_FALSE(manager.get_snapshot());
            const auto sent = raw->sent().size();
            REQUIRE_THROWS(manager.get_blocking(20ms));
            REQUIRE(raw->sent().size() == sent);
        }
        SECTION("timeout late rows cannot satisfy a replacement request even after start") {
            REQUIRE_THROWS(manager.get_blocking(15ms));
            Message request;
            request.id = 0x28;
            request.data[0] = 0xB1;
            for (const auto &reply : rows(request, count, bytes)) {
                raw->inject(reply);
            }
            manager.stop();
            manager.start();
            REQUIRE_THROWS(manager.get_blocking(20ms));
            REQUIRE(raw->sent().size() == 1);
            REQUIRE_FALSE(manager.get_snapshot());
        }
        SECTION("malformed row metadata cannot complete a request") {
            raw->on_request(0x28, [=](const Message &request) {
                auto replies = rows(request, count, bytes);
                replies[0].data[1] |= 1;
                return replies;
            });
            REQUIRE_THROWS(manager.get_blocking(20ms));
            REQUIRE_FALSE(manager.get_snapshot());
        }
        SECTION("FD and extended frames cannot complete a classic request") {
            raw->on_request(0x28, [=](const Message &request) {
                auto replies = rows(request, count, bytes);
                for (auto &reply : replies) {
                    reply.is_fd = true;
                }
                auto more = rows(request, count, bytes);
                for (auto &reply : more) {
                    reply.is_extended = true;
                }
                replies.insert(replies.end(), more.begin(), more.end());
                return replies;
            });
            REQUIRE_THROWS(manager.get_blocking(20ms));
            REQUIRE_FALSE(manager.get_snapshot());
        }
    }

    template <class Hand, class Side> void lifecycle_and_budget(Side side) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        Hand hand(side, std::move(transport), "fake", test_util::unique_mapping_path());
        std::promise<void> sent;
        std::atomic<bool> signalled{false};
        raw->on_request(0x28, [&](const Message &) {
            if (!signalled.exchange(true)) {
                sent.set_value();
            }
            return std::vector<Message>{};
        });
        auto pending = std::async(std::launch::async, [&] {
            try {
                (void)hand.speed.get_blocking(1500ms);
                return false;
            } catch (const std::exception &) {
                return true;
            }
        });
        REQUIRE(sent.get_future().wait_for(500ms) == std::future_status::ready);
        SECTION("queued same-key call spends only its own budget") {
            const auto start = std::chrono::steady_clock::now();
            REQUIRE_THROWS(hand.speed.get_blocking(20ms));
            REQUIRE(std::chrono::steady_clock::now() - start < 200ms);
        }
        const auto start = std::chrono::steady_clock::now();
        hand.stop();
        REQUIRE(pending.wait_for(200ms) == std::future_status::ready);
        REQUIRE(pending.get());
        REQUIRE(std::chrono::steady_clock::now() - start < 250ms);
    }

    template <class Hand, class Side, class Source>
    void polling_stop(Side side, Source source, size_t expected_frames = 1) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        Hand hand(side, std::move(transport), "fake", test_util::unique_mapping_path());
        std::promise<void> sent;
        std::atomic<bool> notified{false};
        raw->on_request(0x28, [&](const Message &) {
            if (!notified.exchange(true)) {
                sent.set_value();
            }
            return std::vector<Message>{};
        });
        REQUIRE_THROWS_AS(hand.start_polling({source}, 0ms), linkerbot::hand::ValidationError);
        hand.start_polling({source, source}, 5s);
        REQUIRE(sent.get_future().wait_for(500ms) == std::future_status::ready);
        const auto start = std::chrono::steady_clock::now();
        hand.stop_polling();
        REQUIRE(std::chrono::steady_clock::now() - start < 200ms);
        // Cancellation may stop a paced query between its banks. It must never
        // send a duplicate batch or continue transmitting after stop returns.
        const auto stopped_frames = raw->sent().size();
        REQUIRE(stopped_frames > 0);
        REQUIRE(stopped_frames <= expected_frames);
        hand.stop();
        REQUIRE_THROWS(hand.start_polling({source}, 10ms));
        REQUIRE(raw->sent().size() == stopped_frames);
    }

    template <class Manager> void copied_callback() {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        linkerbot::can::CANDispatcher dispatcher(std::move(transport));
        std::promise<void> entered, release, tail;
        auto released = release.get_future().share();
        const auto first = dispatcher.subscribe([&](const Message &) {
            entered.set_value();
            released.wait();
        });
        Manager manager(0x28, dispatcher);
        manager.start();
        const auto last = dispatcher.subscribe([&](const Message &) { tail.set_value(); });
        Message reply;
        reply.id = 0x28;
        reply.dlc = 7;
        reply.data[0] = 0x05;
        raw->inject(reply);
        const bool reached = entered.get_future().wait_for(500ms) == std::future_status::ready;
        manager.stop();
        release.set_value();
        REQUIRE(reached);
        REQUIRE(tail.get_future().wait_for(500ms) == std::future_status::ready);
        REQUIRE_FALSE(manager.get_snapshot());
        dispatcher.unsubscribe(first);
        dispatcher.unsubscribe(last);
    }
}

namespace classic_regression {
    class BlockedWriteTransport : public FakeTransport {
    public:
        std::promise<void> entered;
        std::shared_future<void> release;
        std::promise<void> finished;
        void write(const Message &message) override {
            entered.set_value();
            release.wait();
            FakeTransport::write(message);
            finished.set_value();
        }
    };
    template <class Hand, class Side> void send_budget(Side side) {
        auto transport = std::make_unique<BlockedWriteTransport>();
        auto *raw = transport.get();
        std::promise<void> release;
        raw->release = release.get_future().share();
        auto entered = raw->entered.get_future();
        Hand hand(side, std::move(transport), "fake", test_util::unique_mapping_path());
        bool cancel = false;
        SECTION("deadline includes an already claimed backend write") {}
        SECTION("stop cancels receipt wait without waiting for backend") { cancel = true; }
        auto pending = std::async(std::launch::async, [&] {
            try {
                (void)hand.speed.get_blocking(cancel ? 1500ms : 30ms);
                return false;
            } catch (const std::exception &) {
                return true;
            }
        });
        const bool writing = entered.wait_for(500ms) == std::future_status::ready;
        if (cancel) {
            hand.speed.stop();
        }
        const bool ready = pending.wait_for(200ms) == std::future_status::ready;
        release.set_value(); // Always unblock the backend, including a failed assertion.
        REQUIRE(writing);
        REQUIRE(ready);
        REQUIRE(pending.get());
        REQUIRE(raw->finished.get_future().wait_for(500ms) == std::future_status::ready);
        const auto sent = raw->sent().size();
        REQUIRE_THROWS(hand.speed.get_blocking(20ms));
        REQUIRE(raw->sent().size() == sent);
    }

    template <class Hand, class Side, class Source> void retained_lifecycle(Side side, Source source) {
        SECTION("poll send errors remain observable") {
            auto transport = std::make_unique<FakeTransport>();
            transport->fail_writes("rfix offline polling failure");
            Hand hand(side, std::move(transport), "fake", test_util::unique_mapping_path());
            hand.start_polling({source}, 5ms);
            REQUIRE(hand.wait_for_polling_fault(500ms));
            REQUIRE(hand.last_polling_error().has_value());
            hand.stop_polling();
        }
        SECTION("explicit close reports failure and destructor retries without throwing") {
            auto transport = std::make_unique<FakeTransport>();
            auto *raw = transport.get();
            Hand hand(side, std::move(transport), "fake", test_util::unique_mapping_path());
            raw->fail_next_closes(1);
            REQUIRE_THROWS(hand.stop());
            REQUIRE_NOTHROW(hand.stop());
        }
        SECTION("destructor alone absorbs close failure") {
            auto transport = std::make_unique<FakeTransport>();
            transport->fail_next_closes(1);
            { Hand hand(side, std::move(transport), "fake", test_util::unique_mapping_path()); }
            SUCCEED("destructor completed");
        }
    }
}

namespace classic_regression {
    template <class Hand, class Side> void version_window(Side side, int row_count, int offset_step) {
        auto transport = std::make_unique<FakeTransport>();
        auto *raw = transport.get();
        Hand hand(side, std::move(transport), "fake", test_util::unique_mapping_path());
        int mode = 0;
        SECTION("duplicate serial header does not reset the batch") { mode = 1; }
        SECTION("conflicting serial row is rejected") { mode = 2; }
        SECTION("short nonterminal serial row cannot be zero filled into success") { mode = 3; }
        SECTION("all version subqueries share one deadline") { mode = 4; }
        raw->on_request(0x28, [=](const Message &request) {
            if (mode == 4) {
                std::this_thread::sleep_for(30ms);
            }
            if (request.data[0] != 0xC0) {
                Message reply;
                reply.id = 0x28;
                reply.dlc = 4;
                reply.data[0] = request.data[0];
                return std::vector<Message>{reply};
            }
            std::vector<Message> replies;
            for (int i = 0; i < row_count; ++i) {
                Message reply;
                reply.id = 0x28;
                reply.dlc = 8;
                reply.data[0] = 0xC0;
                reply.data[1] = static_cast<uint8_t>(i * offset_step);
                for (int j = 2; j < 8; ++j) {
                    reply.data[j] = static_cast<uint8_t>('A' + i);
                }
                replies.push_back(reply);
            }
            if (mode == 1 || mode == 2) {
                auto duplicate = replies[0];
                if (mode == 2) {
                    duplicate.data[2] = 'Z';
                }
                replies.insert(replies.begin() + 2, duplicate);
            }
            if (mode == 3) {
                replies.front().dlc = 7;
            }
            return replies;
        });
        const auto start = std::chrono::steady_clock::now();
        if (mode == 1) {
            REQUIRE(hand.version.get_device_info(200ms).serial_number.size() == static_cast<size_t>(row_count * 6));
        } else {
            REQUIRE_THROWS(hand.version.get_device_info(mode == 4 ? 50ms : 30ms));
            REQUIRE(std::chrono::steady_clock::now() - start < 150ms);
        }
    }
}

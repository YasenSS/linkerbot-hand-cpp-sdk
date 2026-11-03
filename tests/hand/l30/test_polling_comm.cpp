#include "hand/support/optional_value.hpp"
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l30/events.hpp>
#include <linkerbot/hand/l30/l30.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::FakeTransport;
using linkerbot::hand::l30::kDefaultPollInterval;
using linkerbot::hand::l30::L30;
using linkerbot::hand::l30::SensorSource;

namespace {
    namespace proto = linkerbot::hand::l30::protocol;

    constexpr auto kTimeout = std::chrono::milliseconds{500};

    // Long enough for several ticks of a 17 ms poll, short enough that a broken
    // loop fails instead of hanging.
    constexpr auto kPollWindow = std::chrono::milliseconds{2000};

    bool contains(const std::string &haystack, const std::string &needle) {
        return haystack.find(needle) != std::string::npos;
    }

    // How many query frames for one sensing sub-command reached the bus.
    size_t queries(FakeTransport *transport, uint8_t subcmd) {
        return l30_comm::count_id(transport, l30_comm::query_id(subcmd));
    }

    bool wait_until(const std::function<bool()> &pred, std::chrono::milliseconds limit = kPollWindow) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        return pred();
    }

    // Answers every sensing query so a poll tick completes immediately instead of
    // burning its 100 ms default timeout.
    void install_query_handlers(FakeTransport *transport) {
        transport->on_request(l30_comm::query_id(proto::kSubPosition),
                              l30_comm::i16_handler(l30_comm::filled_i16(450)));
        transport->on_request(l30_comm::query_id(proto::kSubSpeed), l30_comm::i16_handler(l30_comm::filled_i16(120)));
        transport->on_request(l30_comm::query_id(proto::kSubCurrent), l30_comm::i16_handler(l30_comm::filled_i16(-70)));
        transport->on_request(l30_comm::query_id(proto::kSubTemperature),
                              l30_comm::u8_handler(l30_comm::filled_u8(37)));
        transport->on_request(l30_comm::query_id(proto::kSubFault), l30_comm::u8_handler(l30_comm::filled_u8(0)));
    }

    std::unique_ptr<L30> make_hand(FakeTransport **out, bool with_handlers = true) {
        auto transport = std::make_unique<FakeTransport>();
        *out = transport.get();
        l30_comm::install_periodic_acks(*out);
        if (with_handlers) {
            install_query_handlers(*out);
        }
        return std::make_unique<L30>(std::move(transport), l30_comm::kNodeId, l30_comm::kHostId, false);
    }
} // namespace

// FakeTransport：主机侧轮询——每个源一条线程发阻塞读，默认只轮询角度，
// 重复 start 是替换而非叠加，单次 tick 失败也不能弄死线程。

// start_polling({}) 默认只轮询角度，按 kDefaultPollInterval(17ms) 反复发
// 0x00A02100 查询帧。
TEST_CASE("comm: start_polling defaults to angle only", "[comm][l30][polling]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);
    raw->clear_sent();

    REQUIRE(kDefaultPollInterval == std::chrono::milliseconds{17});
    hand->start_polling({});
    const auto polled = wait_until([raw] { return queries(raw, proto::kSubPosition) >= 3; });
    hand->stop_polling();

    REQUIRE(polled);
    REQUIRE(queries(raw, proto::kSubSpeed) == 0);
    REQUIRE(queries(raw, proto::kSubFault) == 0);

    hand->stop();
}

// 非正的轮询间隔应抛 invalid_argument，消息里点名是哪个源。
TEST_CASE("comm: a non-positive interval names the offending source", "[comm][l30][polling]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    std::string message;
    try {
        hand->start_polling({{SensorSource::kTemperature, std::chrono::milliseconds{0}}});
    } catch (const linkerbot::hand::ValidationError &error) {
        message = error.what();
    }
    REQUIRE(contains(message, "temperature"));
    REQUIRE(contains(message, "must be positive"));

    REQUIRE_THROWS_AS(hand->start_polling({{SensorSource::kSpeed, std::chrono::milliseconds{-5}}}),
                      linkerbot::hand::ValidationError);

    hand->stop();
}

// 同时轮询五个源，五个查询 ID 都应上总线，snapshot 也都应填上。
TEST_CASE("comm: polling several sources emits every query ID", "[comm][l30][polling]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);
    raw->clear_sent();

    hand->start_polling({
        {SensorSource::kAngle, std::chrono::milliseconds{20}},
        {SensorSource::kSpeed, std::chrono::milliseconds{20}},
        {SensorSource::kCurrent, std::chrono::milliseconds{20}},
        {SensorSource::kTemperature, std::chrono::milliseconds{20}},
        {SensorSource::kFault, std::chrono::milliseconds{20}},
    });
    const auto all_seen = wait_until([raw] {
        return queries(raw, proto::kSubPosition) > 0 && queries(raw, proto::kSubSpeed) > 0 &&
               queries(raw, proto::kSubCurrent) > 0 && queries(raw, proto::kSubTemperature) > 0 &&
               queries(raw, proto::kSubFault) > 0;
    });
    hand->stop_polling();

    REQUIRE(all_seen);

    const auto snapshot = hand->get_snapshot();
    REQUIRE(snapshot.angle.has_value());
    REQUIRE(snapshot.speed.has_value());
    REQUIRE(snapshot.current.has_value());
    REQUIRE(snapshot.temperature.has_value());
    REQUIRE(snapshot.fault.has_value());

    hand->stop();
}

// stop_polling() 要 join 掉所有线程，调两次都应成功且不卡。
TEST_CASE("comm: stop_polling joins the threads and is idempotent", "[comm][l30][polling]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    hand->start_polling({{SensorSource::kAngle, std::chrono::milliseconds{20}}});
    REQUIRE(wait_until([raw] { return queries(raw, proto::kSubPosition) > 0; }));

    REQUIRE_NOTHROW(hand->stop_polling());
    REQUIRE_NOTHROW(hand->stop_polling());

    // Joined for real: nothing new lands on the bus after the second stop.
    raw->clear_sent();
    std::this_thread::sleep_for(std::chrono::milliseconds{120});
    REQUIRE(raw->sent().empty());

    hand->stop();
}

// 再调一次 start_polling 是替换而不是叠加：换成只轮询速度之后，
// 角度查询必须彻底停下来。
TEST_CASE("comm: start_polling replaces the previous threads", "[comm][l30][polling]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    hand->start_polling({{SensorSource::kAngle, std::chrono::milliseconds{20}}});
    REQUIRE(wait_until([raw] { return queries(raw, proto::kSubPosition) > 0; }));

    hand->start_polling({{SensorSource::kSpeed, std::chrono::milliseconds{20}}});
    REQUIRE(wait_until([raw] { return queries(raw, proto::kSubSpeed) > 0; }));

    raw->clear_sent();
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    const auto angle_after = queries(raw, proto::kSubPosition);
    const auto speed_after = queries(raw, proto::kSubSpeed);
    hand->stop_polling();

    REQUIRE(angle_after == 0);
    REQUIRE(speed_after > 0);

    hand->stop();
}

// 无线上序号时超时源必须显式隔离，不能在旧请求仍可能到达时自动重发。
TEST_CASE("comm: a timed out poll exposes quarantine and stops resending", "[comm][l30][polling]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw, false);
    raw->clear_sent();
    hand->start_polling({{SensorSource::kAngle, std::chrono::milliseconds{20}}});
    const auto isolated = wait_until([&] {
        const auto error = hand->polling_error(SensorSource::kAngle);
        if (!error) {
            return false;
        }
        try {
            std::rethrow_exception(error);
        } catch (const linkerbot::hand::l30::Client::QuarantinedError &) {
            return true;
        } catch (...) {
            return false;
        }
    });
    hand->stop_polling();
    REQUIRE(isolated);
    REQUIRE(queries(raw, proto::kSubPosition) == 1);
    REQUIRE_FALSE(hand->get_snapshot().angle.has_value());
    install_query_handlers(raw);
    REQUIRE_THROWS_AS(hand->angle.get_blocking(kTimeout), linkerbot::hand::l30::Client::QuarantinedError);
    REQUIRE_NOTHROW(hand->speed.get_blocking(kTimeout));
    hand->stop();
    // FakeTransport reconstruction has no stale inbox: the explicit recovery
    // boundary can issue the key again and populate the ordinary snapshot.
    hand = make_hand(&raw);
    hand->start_polling({{SensorSource::kAngle, std::chrono::milliseconds{20}}});
    const auto recovered = wait_until([&] { return hand->get_snapshot().angle.has_value(); });
    hand->stop_polling();
    REQUIRE(recovered);
    hand->stop();
}

// stop() 会顺手 stop_polling()：拆解之后总线上不该再有查询帧。
TEST_CASE("comm: stop also stops polling", "[comm][l30][polling]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    hand->start_polling({{SensorSource::kAngle, std::chrono::milliseconds{20}}});
    REQUIRE(wait_until([raw] { return queries(raw, proto::kSubPosition) > 0; }));

    REQUIRE_NOTHROW(hand->stop());

    const auto before = raw->sent().size();
    std::this_thread::sleep_for(std::chrono::milliseconds{120});
    REQUIRE(raw->sent().size() == before);
}

// 轮询确实在刷新：隔一会儿再拍一次，角度时间戳应该更新了。
TEST_CASE("comm: polling keeps refreshing the snapshot", "[comm][l30][polling]") {
    FakeTransport *raw = nullptr;
    auto hand = make_hand(&raw);

    hand->start_polling({{SensorSource::kAngle, std::chrono::milliseconds{20}}});
    REQUIRE(wait_until([&hand] { return hand->get_snapshot().angle.has_value(); }));

    const auto first = hand->get_snapshot();
    REQUIRE(first.angle.has_value());
    const auto stamp = test_support::require_value(first.angle).timestamp;

    const auto refreshed = wait_until([&hand, stamp] {
        const auto snapshot = hand->get_snapshot();
        return snapshot.angle.has_value() && snapshot.angle->timestamp > stamp;
    });
    hand->stop_polling();

    REQUIRE(refreshed);

    hand->stop();
}

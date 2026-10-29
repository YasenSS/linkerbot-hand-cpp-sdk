#include "hand/o30/comm_helpers.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <linkerbot/hand/errors.hpp>

using namespace linkerbot::hand::o30;
using namespace std::chrono_literals;
using linkerbot::can::Message;

TEST_CASE("comm: O30i standard ID changes follow old or new ACK IDs", "[comm][o30][alignment][config]") {
    const auto new_ack = GENERATE(false, true);
    const auto raw_api = GENERATE(false, true);
    auto h = o30_comm::make_hand();
    // Prime the cached revision; the new endpoint must be rediscovered.
    (void)h.hand->fault.get_blocking();
    h.transport->on_request(1, [new_ack](const Message &req) {
        if (o30_comm::hop_useful(req) != std::vector<uint8_t>{0x35, 0, 2, 0x23, 1}) {
            return std::vector<Message>{};
        }
        return std::vector<Message>{o30_comm::hop_reply(new_ack ? 0x523 : 0x401, 0x35, false, 0, {0x23, 1})};
    });
    auto next = std::make_shared<o30_comm::HopEndpoint>();
    next->request_id = 0x123;
    next->seed_defaults();
    next->put_ascii(0x41, 0x61, 8, "0.0.4");
    next->put(0x0C, std::vector<uint8_t>(36, 0));
    next->put(0x0D, std::vector<uint8_t>(36, 0x0B));
    o30_comm::attach_hop(h.transport, next);
    if (raw_api) {
        h.hand->config.write_field(0, {0x23, 1});
    } else {
        h.hand->config.set_standard_id(0x123);
    }
    REQUIRE(h.hand->request_id() == 0x123);
    REQUIRE(h.hand->response_id() == 0x523);
    h.transport->clear_sent();
    const auto fault = h.hand->fault.get_blocking();
    for (const auto v : fault.faults) {
        REQUIRE(v == 0);
    }
    const auto sent = h.transport->sent();
    REQUIRE(sent.size() == 2);
    REQUIRE(sent[0].id == 0x123);
    REQUIRE(o30_comm::hop_useful(sent[0]) == std::vector<uint8_t>{0x41, 0x61, 8});
    REQUIRE(o30_comm::hop_useful(sent[1]) == std::vector<uint8_t>{0x0C, 0, 36});
}

TEST_CASE("comm: O30i unsuccessful standard ID writes do not retarget the client", "[comm][o30][alignment][config]") {
    const auto failure = GENERATE(0, 1, 2);
    auto h = o30_comm::make_hand();
    h.transport->on_request(1, [failure](const Message &) {
        if (failure == 0) {
            return std::vector<Message>{};
        }
        if (failure == 1) {
            return std::vector<Message>{o30_comm::error_reply(0x401, 0, 0x40)};
        }
        return std::vector<Message>{o30_comm::hop_reply(0x523, 0x35, false, 0, {0x23})};
    });
    REQUIRE_THROWS_AS(h.hand->config.set_standard_id(0x123, 10ms), std::runtime_error);
    REQUIRE(h.hand->request_id() == 1);
    REQUIRE(h.hand->response_id() == 0x401);
}

TEST_CASE("comm: O30i config validates CAN IDs field bounds and explicit triggers", "[comm][o30][alignment][config]") {
    auto h = o30_comm::make_hand();
    REQUIRE_THROWS_AS(h.hand->config.set_standard_id(0x400), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->config.set_extended_id(0x20000000), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->config.write_field(0, {1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->config.write_field(1, {1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->config.write_field(31, {1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->config.write_field(0x1E, {1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->config.write_field(0x1B, {0, 1}), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->config.restore_config(false), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->config.restore_all(false), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(h.hand->config.save_to_flash(false), linkerbot::hand::ValidationError);
    REQUIRE(h.transport->sent().empty());
    h.hand->config.set_extended_id(0x12345678);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x35, 2, 4, 0x78, 0x56, 0x34, 0x12});
    h.hand->config.set_control_mode(2);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x35, 0x18, 1, 2});
    h.hand->config.set_buzzer(1);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x35, 0x1A, 1, 1});
    h.hand->config.restore_config(true);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x35, 0x1C, 1, 1});
    h.hand->config.restore_all(true);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x35, 0x1D, 1, 1});
    h.hand->config.save_to_flash(true);
    REQUIRE(o30_comm::last_payload(h.transport) == std::vector<uint8_t>{0x35, 0x1E, 1, 1});
}

TEST_CASE("comm: O30i rejects invalid request addresses and malformed response lengths",
          "[comm][o30][alignment][protocol]") {
    REQUIRE(protocol::expected_response_id(0x3FF) == 0x7FF);
    REQUIRE_THROWS_AS(protocol::expected_response_id(0x400), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(protocol::build_message(0x400, 1, 0, 1, {}, false), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(protocol::build_message(1, 1, 0, 0, {}, false), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(protocol::build_message(1, 1, 255, 2, {}, false), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(protocol::build_message(1, 1, 0, 2, {1}, false), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(protocol::build_message(1, 1, 255, 1, {}, false));
    auto bytes = std::vector<uint8_t>(65, 0);
    bytes[0] = 1;
    bytes[2] = 62;
    REQUIRE_THROWS_AS(protocol::parse_frame(bytes.data(), bytes.size()), protocol::ProtocolError);
    bytes.resize(64);
    REQUIRE_THROWS_AS(protocol::parse_frame(bytes.data(), bytes.size()), protocol::ProtocolError);
    bytes[2] = 0;
    REQUIRE_THROWS_AS(protocol::parse_frame(bytes.data(), bytes.size()), protocol::ProtocolError);
    bytes[1] = 255;
    bytes[2] = 2;
    REQUIRE_THROWS_AS(protocol::parse_frame(bytes.data(), bytes.size()), protocol::ProtocolError);
}

TEST_CASE("comm: O30i ignores unrelated malformed frames and wrong transport flags", "[comm][o30][alignment][client]") {
    auto h = o30_comm::make_hand();
    h.transport->on_request(1, [](const Message &) {
        auto valid = o30_comm::hop_reply(0x401, 1, false, 0, std::vector<uint8_t>(36, 42));
        auto unrelated = valid;
        unrelated.data[0] = 2;
        unrelated.dlc = 3;
        auto wrong_rts = unrelated;
        wrong_rts.data[0] = 0x81;
        auto classic = valid;
        classic.is_fd = false;
        auto extended = valid;
        extended.is_extended = true;
        auto wrong_id = valid;
        wrong_id.id = 0x402;
        return std::vector<Message>{unrelated, wrong_rts, classic, extended, wrong_id, valid};
    });
    REQUIRE(h.hand->angle.get_blocking().angles.to_raw() == o30_comm::filled_u8(42));
}

TEST_CASE("comm: O30i duplicate fragments are accepted only when bytes agree", "[comm][o30][alignment][client]") {
    const auto conflict = GENERATE(false, true);
    auto h = o30_comm::make_hand();
    h.transport->on_request(1, [conflict](const Message &) {
        auto first = o30_comm::hop_reply(0x401, 0x20, false, 0, std::vector<uint8_t>(61, 1));
        auto duplicate = first;
        if (conflict) {
            duplicate.data[3] = 2;
        }
        auto last = o30_comm::hop_reply(0x401, 0x20, false, 61, std::vector<uint8_t>(11, 1));
        return std::vector<Message>{first, duplicate, last};
    });
    if (conflict) {
        REQUIRE_THROWS_AS(h.hand->angle.get_halfword(), protocol::ProtocolError);
    } else {
        for (const auto raw : h.hand->angle.get_halfword().positions) {
            REQUIRE(raw == 0x0101);
        }
    }
}

TEST_CASE("comm: O30i invalid current writes fail before revision discovery", "[comm][o30][alignment][client]") {
    auto transport = std::make_unique<linkerbot::can::FakeTransport>();
    auto *raw = transport.get();
    linkerbot::can::CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);
    REQUIRE_THROWS_AS(client.write(0x04, 0, {}, 50ms), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(client.write(0x04, 0, std::vector<uint8_t>(62, 1), 50ms), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(client.write(0x04, 255, {1, 2}, 50ms), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(client.write(0x84, 0, {1}, 50ms), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(client.write(0x04, 0, {1}, 0ms), linkerbot::hand::ValidationError);
    REQUIRE(raw->sent().empty());
}

#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/o30/client.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

#include "hand/o30/comm_helpers.hpp"

using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::o30::Client;
using linkerbot::hand::o30::protocol::ProtocolError;

namespace {
    namespace proto = linkerbot::hand::o30::protocol;

    constexpr auto kTimeout = o30_comm::kTimeout;

    bool contains(const std::string &haystack, const std::string &needle) {
        return haystack.find(needle) != std::string::npos;
    }
} // namespace

// FakeTransport：Client 串行事务、分片重组、超时、关闭、4F 错误。

// request_id 必须让应答 id+0x400 仍是 11-bit 标准帧。
TEST_CASE("comm: client rejects a request ID that cannot stay 11-bit", "[comm][o30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));

    REQUIRE_THROWS_AS(Client(dispatcher, 0x400), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Client(dispatcher, 0x800), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(Client(dispatcher, 0x001));
    REQUIRE_NOTHROW(Client(dispatcher, 0x123));

    dispatcher.stop();
}

// request_id / response_id 原样回读，含非默认值。
TEST_CASE("comm: client reports request and response IDs", "[comm][o30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 0x123);

    REQUIRE(client.request_id() == 0x123);
    REQUIRE(client.response_id() == 0x523);

    dispatcher.stop();
}

// 读 36 字节：发 01 00 24，标准 FD 无 BRS，重组后长度 36。
TEST_CASE("comm: read sends a 3-byte HOP header and returns the object", "[comm][o30][client]") {
    auto hop = std::make_shared<o30_comm::HopEndpoint>();
    hop->seed_defaults();
    hop->put(proto::kMiPosition, o30_comm::ramp_slots());
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    o30_comm::attach_hop(raw, hop);
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);

    const auto payload = client.read(proto::kMiPosition, 0, proto::kU8ObjectBytes, false, kTimeout);
    REQUIRE(payload == o30_comm::ramp_slots());

    const auto sent = raw->sent();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].id == 0x001);
    REQUIRE_FALSE(sent[0].is_extended);
    REQUIRE(sent[0].is_fd);
    REQUIRE_FALSE(sent[0].is_brs);
    REQUIRE(o30_comm::hop_useful(sent[0]) == std::vector<uint8_t>({0x01, 0x00, 0x24}));

    dispatcher.stop();
}

// 写三段装配槽：SI 0/5/16，长度 1/10/9。
TEST_CASE("comm: write_mounted emits the three safe segments", "[comm][o30][client]") {
    auto hop = std::make_shared<o30_comm::HopEndpoint>();
    hop->seed_defaults();
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    o30_comm::attach_hop(raw, hop);
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);

    std::array<uint8_t, linkerbot::hand::o30::kSlotCount> slots{};
    slots[0] = 0x80;
    for (uint8_t i = 5; i < 15; ++i) {
        slots[i] = 0x80;
    }
    for (uint8_t i = 16; i < 25; ++i) {
        slots[i] = 0x80;
    }
    client.write_mounted(proto::kMiPosition, slots, kTimeout);

    REQUIRE(hop->writes.size() == 3);
    REQUIRE(hop->writes[0].si == 0);
    REQUIRE(hop->writes[0].payload == std::vector<uint8_t>({0x80}));
    REQUIRE(hop->writes[1].si == 5);
    REQUIRE(hop->writes[1].payload.size() == 10);
    REQUIRE(hop->writes[2].si == 16);
    REQUIRE(hop->writes[2].payload.size() == 9);

    const auto sent = raw->sent();
    REQUIRE(sent.size() == 3);
    REQUIRE(sent[1].dlc == 16);
    REQUIRE(sent[2].dlc == 12);

    dispatcher.stop();
}

// 225 字节产品信息按 61+61+61+42 重组。
TEST_CASE("comm: client reassembles SI-chained fragments", "[comm][o30][client]") {
    auto hop = std::make_shared<o30_comm::HopEndpoint>();
    hop->seed_defaults();
    auto transport = std::make_unique<FakeTransport>();
    o30_comm::attach_hop(transport.get(), hop);
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);

    const auto blob = client.read(proto::kMiProductInfo, 0, proto::kProductInfoBytes, false, kTimeout);
    REQUIRE(blob.size() == proto::kProductInfoBytes);
    REQUIRE(blob[0] == static_cast<uint8_t>('O'));
    REQUIRE(blob[1] == static_cast<uint8_t>('3'));

    dispatcher.stop();
}

// 72 字节半字位置：61+11 两帧。
TEST_CASE("comm: 72-byte i16 object is reassembled from two fragments", "[comm][o30][client]") {
    auto hop = std::make_shared<o30_comm::HopEndpoint>();
    hop->seed_defaults();
    std::vector<uint8_t> payload(proto::kU16ObjectBytes, 0);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i);
    }
    hop->put(proto::kMiPosition16, payload);
    auto transport = std::make_unique<FakeTransport>();
    o30_comm::attach_hop(transport.get(), hop);
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);

    const auto body = client.read(proto::kMiPosition16, 0, proto::kU16ObjectBytes, false, kTimeout);
    REQUIRE(body == payload);

    dispatcher.stop();
}

// 越界 SI 读返回 4F 02，Client 抛 ProtocolError 并带 error_code。
TEST_CASE("comm: missing sub-index surfaces as a HOP error", "[comm][o30][client]") {
    auto hop = std::make_shared<o30_comm::HopEndpoint>();
    hop->put(proto::kMiPosition, std::vector<uint8_t>(8, 0));
    auto transport = std::make_unique<FakeTransport>();
    o30_comm::attach_hop(transport.get(), hop);
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);

    uint8_t code = 0xFF;
    std::string message;
    try {
        static_cast<void>(client.read(proto::kMiPosition, 0, 36, false, kTimeout));
    } catch (const ProtocolError &error) {
        code = error.error_code();
        message = error.what();
    }
    REQUIRE(code == proto::kErrorSubIndexMissing);
    REQUIRE(contains(message, "0x02"));
    // A complete negative ACK releases correlation; only ambiguous fragments
    // require recovery. The next valid read uses the same runtime key.
    REQUIRE(client.read(proto::kMiPosition, 0, 8, false, kTimeout).size() == 8);

    dispatcher.stop();
}

// 没人应答则超时，文案带毫秒数。
TEST_CASE("comm: read times out when no reply arrives", "[comm][o30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);

    std::string message;
    try {
        static_cast<void>(client.read(proto::kMiPosition, 0, 36, false, std::chrono::milliseconds{30}));
    } catch (const std::runtime_error &error) {
        message = error.what();
    }
    REQUIRE(contains(message, "No O30 response received within"));
    REQUIRE(contains(message, "30ms"));

    dispatcher.stop();
}

// close() 之后拒绝读写；close 可重复。
TEST_CASE("comm: a closed client refuses further requests", "[comm][o30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);

    REQUIRE_NOTHROW(client.close());
    REQUIRE_NOTHROW(client.close());

    std::string message;
    try {
        static_cast<void>(client.read(proto::kMiPosition, 0, 1, false, kTimeout));
    } catch (const std::runtime_error &error) {
        message = error.what();
    }
    REQUIRE(contains(message, "O30 client is closed"));

    dispatcher.stop();
}

// 空写载荷应拒。
TEST_CASE("comm: write rejects an empty payload", "[comm][o30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);

    REQUIRE_THROWS_AS(client.write(proto::kMiPosition, 0, {}, kTimeout), linkerbot::hand::ValidationError);

    dispatcher.stop();
}

// 扩展帧应答应被丢掉，最终超时。
TEST_CASE("comm: client drops extended-id replies", "[comm][o30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(0x001, [](const Message &req) {
        Message reply;
        reply.id = 0x401;
        reply.is_extended = true;
        reply.is_fd = true;
        reply.dlc = 4;
        reply.data[0] = req.data[0];
        reply.data[1] = req.data[1];
        reply.data[2] = 1;
        reply.data[3] = 0x00;
        return std::vector<Message>{reply};
    });
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);

    REQUIRE_THROWS_AS(client.read(proto::kMiPosition, 0, 1, false, std::chrono::milliseconds{40}), std::runtime_error);

    dispatcher.stop();
}

// set_request_id 在事务外可改，随后应答走新 ID。
TEST_CASE("comm: set_request_id retargets later transactions", "[comm][o30][client]") {
    auto hop = std::make_shared<o30_comm::HopEndpoint>();
    hop->request_id = 0x123;
    hop->seed_defaults();
    hop->put(proto::kMiSpeed, o30_comm::ramp_slots());
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    o30_comm::attach_hop(raw, hop);
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 0x001);

    client.set_request_id(0x123);
    REQUIRE(client.response_id() == 0x523);
    const auto payload = client.read(proto::kMiSpeed, 0, proto::kU8ObjectBytes, false, kTimeout);
    REQUIRE(payload.size() == proto::kU8ObjectBytes);
    REQUIRE(raw->sent().back().id == 0x123);

    dispatcher.stop();
}

#include <future>
#include <linkerbot/can/errors.hpp>

TEST_CASE("comm: O30 request deadline returns during claimed backend IO", "[comm][o30][client][regression]") {
    using namespace std::chrono_literals;
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->arm_tx_gate();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);
    auto call = std::async(std::launch::async, [&] {
        try {
            (void)client.read(proto::kMiPosition, 0, 1, false, 50ms);
            return std::string("success");
        } catch (const linkerbot::hand::TimeoutError &e) {
            return std::string(e.what());
        }
    });
    const bool entered = raw->wait_until_tx_entered(500ms);
    const auto returned = call.wait_for(250ms);
    raw->release_tx_gate();
    const auto error = call.get();
    REQUIRE(entered);
    REQUIRE(returned == std::future_status::ready);
    REQUIRE(error.find("claimed backend I/O") != std::string::npos);
    // A second same-key request cannot consume the first one's late response.
    REQUIRE_THROWS(client.read(proto::kMiPosition, 0, 1, false, 20ms));
    client.close();
    REQUIRE(raw->sent().size() == 1);
    dispatcher.stop();
}

TEST_CASE("comm: O30 semantic admission respects caller deadline", "[comm][o30][client][regression]") {
    using namespace std::chrono_literals;
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);
    std::future<bool> call;
    std::future_status returned;
    {
        auto operation = client.operation(1s);
        call = std::async(std::launch::async, [&] {
            try {
                (void)client.read(proto::kMiPosition, 0, 1, false, 20ms);
                return false;
            } catch (const linkerbot::hand::TimeoutError &) {
                return true;
            }
        });
        returned = call.wait_for(250ms);
    }
    REQUIRE(returned == std::future_status::ready);
    REQUIRE(call.get());
    REQUIRE(raw->sent().empty());
    client.close();
    dispatcher.stop();
}

TEST_CASE("comm: O30 mounted writes cannot interleave their segments", "[comm][o30][client][regression]") {
    using namespace std::chrono_literals;
    auto hop = std::make_shared<o30_comm::HopEndpoint>();
    hop->seed_defaults();
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    o30_comm::attach_hop(raw, hop);
    raw->arm_tx_gate();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher);
    std::array<uint8_t, linkerbot::hand::o30::kSlotCount> a{}, b{};
    a.fill(11);
    b.fill(22);
    auto first = std::async(std::launch::async, [&] { client.write_mounted(proto::kMiPosition, a, 1s); });
    const bool entered = raw->wait_until_tx_entered(500ms);
    auto second = std::async(std::launch::async, [&] { client.write_mounted(proto::kMiPosition, b, 1s); });
    raw->release_tx_gate();
    first.get();
    second.get();
    REQUIRE(entered);
    REQUIRE(hop->writes.size() == 6);
    for (size_t i = 0; i < 6; ++i) {
        REQUIRE(hop->writes[i].payload.front() == (i < 3 ? 11 : 22));
    }
    client.close();
    dispatcher.stop();
}

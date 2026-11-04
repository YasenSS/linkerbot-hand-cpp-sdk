#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/errors.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/l30/protocol.hpp>

#include "hand/l30/comm_helpers.hpp"

using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::can::Message;
using linkerbot::hand::l30::Client;
using linkerbot::hand::l30::protocol::ProtocolError;

namespace {
    namespace proto = linkerbot::hand::l30::protocol;

    constexpr auto kTimeout = std::chrono::milliseconds{500};

    // Golden arbitration IDs for node 1 / host 0, taken from the protocol tables
    // rather than recomputed, so a change in the bit layout fails here.
    constexpr uint32_t kEnableRequestId = 0x0220E100;
    constexpr uint32_t kPositionWriteId = 0x02202100;
    constexpr uint32_t kPositionQueryId = 0x00A02100;
    constexpr uint32_t kPeriodicConfigAckId = 0x02802008;

    // Bounded poll: a regression must fail rather than hang, so every wait has a
    // deadline and the predicate is re-checked once after it expires.
    bool wait_until(const std::function<bool()> &pred, std::chrono::milliseconds limit = kTimeout) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        return pred();
    }

    bool contains(const std::string &haystack, const std::string &needle) {
        return haystack.find(needle) != std::string::npos;
    }

    Message plain_frame(uint32_t id, const std::vector<uint8_t> &payload) {
        // Deliberately NOT extended: Client::on_message must drop it.
        Message msg;
        msg.id = id;
        msg.dlc = static_cast<uint8_t>(payload.size());
        for (size_t i = 0; i < payload.size(); ++i) {
            msg.data[i] = payload[i];
        }
        return msg;
    }
} // namespace

// FakeTransport：Client 这一层的请求/应答路由——ID 组装、超时、关闭唤醒、
// 周期报告分发，以及触觉两帧重组不会挡住普通请求（Python fix#141 回归）。

// node_id 必须落在 1..31、host_id 落在 0..31，越界应抛 invalid_argument。
TEST_CASE("comm: client rejects out-of-range addresses", "[comm][l30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));

    REQUIRE_THROWS_AS(Client(dispatcher, 0, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Client(dispatcher, 32, 0), linkerbot::hand::ValidationError);
    REQUIRE_THROWS_AS(Client(dispatcher, 1, 32), linkerbot::hand::ValidationError);
    REQUIRE_NOTHROW(Client(dispatcher, 1, 0));

    dispatcher.stop();
}

// node_id() / host_id() 应原样回读构造参数，含非默认值。
TEST_CASE("comm: client reports its own addresses", "[comm][l30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, 7, 3);

    REQUIRE(client.node_id() == 7);
    REQUIRE(client.host_id() == 3);

    dispatcher.stop();
}

// send_no_ack 只发一帧、不等应答：ID/DLC 精确，且调用立即返回。
TEST_CASE("comm: send_no_ack writes one frame without waiting", "[comm][l30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    const auto started = std::chrono::steady_clock::now();
    client.send_no_ack(proto::kParentControl, proto::kSubPosition, proto::encode_i16_vector(l30_comm::filled_i16(100)),
                       proto::kVectorWireBytes);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    const auto sent = raw->sent();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].id == kPositionWriteId);
    REQUIRE(sent[0].is_extended);
    REQUIRE(sent[0].is_fd);
    REQUIRE(sent[0].dlc == proto::kVectorWireBytes);
    REQUIRE(elapsed < std::chrono::milliseconds{100});

    dispatcher.stop();
}

// request_ack 正常路径：请求帧 ID/字节精确，返回值就是原始 ACK 负载。
TEST_CASE("comm: request_ack returns the raw ACK payload", "[comm][l30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kEnableRequestId, l30_comm::ack_handler());

    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    const auto ack = client.request_ack(proto::kParentControl, proto::kSubEnable, proto::empty_request_payload(),
                                        proto::kEmptyRequestBytes, kTimeout);

    REQUIRE(ack == std::vector<uint8_t>{0x00, 0x00, proto::kStatusOk});

    const auto sent = raw->sent();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].id == kEnableRequestId);
    REQUIRE(sent[0].dlc == proto::kEmptyRequestBytes);
    REQUIRE(l30_comm::trimmed(sent[0]) == std::vector<uint8_t>{0x00, 0x00});

    dispatcher.stop();
}

// 非零状态的 ACK 照样原样返回；解码状态是 check_response_status 的职责，
// 它应抛 ProtocolError 并把设备状态码带出来。
TEST_CASE("comm: non-zero ACK status surfaces through check_response_status", "[comm][l30][client]") {
    constexpr uint8_t kNotUnlocked = 0x20;

    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(kEnableRequestId, l30_comm::ack_handler(kNotUnlocked));

    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    const auto ack = client.request_ack(proto::kParentControl, proto::kSubEnable, proto::empty_request_payload(),
                                        proto::kEmptyRequestBytes, kTimeout);
    REQUIRE(ack.size() == proto::kAckBytes);
    REQUIRE(ack[proto::kStatusByteIndex] == kNotUnlocked);

    uint8_t reported = proto::kStatusOk;
    try {
        proto::check_response_status(ack.data(), ack.size());
    } catch (const ProtocolError &error) {
        reported = error.status();
    }
    REQUIRE(reported == kNotUnlocked);

    dispatcher.stop();
}

// 没人应答时应超时抛 runtime_error，消息里带固定前缀和超时值。
TEST_CASE("comm: request times out with the configured deadline", "[comm][l30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    std::string message;
    try {
        static_cast<void>(client.read(proto::kParentQuery, proto::kSubPosition, std::chrono::milliseconds{30}));
    } catch (const std::runtime_error &error) {
        message = error.what();
    }
    REQUIRE(contains(message, "No L30 response received within"));
    REQUIRE(contains(message, "30ms"));

    dispatcher.stop();
}

// read 发的是 Access=0、空负载（BYTE0/BYTE1 全 0）、DLC=2 的查询帧。
TEST_CASE("comm: read sends an empty Access=0 request", "[comm][l30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(kPositionQueryId, l30_comm::i16_handler(l30_comm::filled_i16(0)));

    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    const auto response = client.read(proto::kParentQuery, proto::kSubPosition, kTimeout);
    // A request response carries a status byte, so data starts at BYTE3.
    REQUIRE(response.size() == proto::kResponseDataOffset + proto::kVectorPayloadBytes);

    const auto sent = raw->sent();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].id == kPositionQueryId);
    REQUIRE(sent[0].dlc == proto::kEmptyRequestBytes);
    REQUIRE(l30_comm::trimmed(sent[0]) ==
            std::vector<uint8_t>{proto::kEmptyPayloadLength, proto::kSingleFrameTransaction});

    dispatcher.stop();
}

// close() 之后任何请求都应抛「L30 client is closed」，且 close() 可重复调。
TEST_CASE("comm: a closed client refuses further requests", "[comm][l30][client]") {
    auto transport = std::make_unique<FakeTransport>();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    REQUIRE_NOTHROW(client.close());
    REQUIRE_NOTHROW(client.close());

    std::optional<linkerbot::hand::ErrorCode> error_code;
    try {
        static_cast<void>(client.read(proto::kParentQuery, proto::kSubPosition, kTimeout));
    } catch (const linkerbot::hand::HandError &error) {
        error_code = error.code();
    }
    REQUIRE(error_code == linkerbot::hand::ErrorCode::kClosed);

    REQUIRE_THROWS_AS(client.send_no_ack(proto::kParentControl, proto::kSubPosition, proto::empty_request_payload(),
                                         proto::kEmptyRequestBytes),
                      linkerbot::hand::ClosedError);

    dispatcher.stop();
}

// close() 应唤醒另一个线程里正在等应答的读：用 tx gate 把发送卡在总线上，
// 确保 waiter 已注册再 close，读方必须立刻带「closed」抛出而不是等到超时。
TEST_CASE("comm: close wakes a request blocked in another thread", "[comm][l30][client]") {
    std::atomic<bool> finished{false};
    std::string message;
    std::optional<linkerbot::hand::ErrorCode> error_code;

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    raw->arm_tx_gate();
    std::thread reader([&] {
        try {
            static_cast<void>(client.read(proto::kParentQuery, proto::kSubPosition, std::chrono::milliseconds{5000}));
        } catch (const linkerbot::hand::HandError &error) {
            message = error.what();
            error_code = error.code();
        }
        finished = true;
    });

    const auto entered = raw->wait_until_tx_entered(kTimeout);
    auto closing = std::async(std::launch::async, [&] { client.close(); });
    const auto cancelled_before_release = wait_until([&] { return finished.load(); });
    const auto draining = closing.wait_for(std::chrono::milliseconds{20});
    raw->release_tx_gate();
    closing.get();
    REQUIRE(cancelled_before_release);
    REQUIRE(draining == std::future_status::timeout);
    const auto woke = wait_until([&] { return finished.load(); }, std::chrono::milliseconds{2000});
    reader.join();

    REQUIRE(entered);
    REQUIRE(woke);
    REQUIRE(error_code == linkerbot::hand::ErrorCode::kLifecycle);
    REQUIRE_FALSE(message.empty());

    dispatcher.stop();
}

// 非扩展帧应被直接丢掉：先注入一帧标准帧，再注入同内容的扩展帧，
// 回调只应被调用一次。
TEST_CASE("comm: a non-extended frame never reaches a handler", "[comm][l30][client]") {
    std::atomic<int> calls{0};

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    const auto handler_id =
        client.add_report_handler(proto::kSubPosition, [&calls](const std::vector<uint8_t> &) { ++calls; });
    static_cast<void>(handler_id);

    const auto payload = l30_comm::periodic_i16_payload(l30_comm::filled_i16(50));
    raw->inject(plain_frame(l30_comm::report_id(proto::kSubPosition), payload));
    raw->inject(l30_comm::periodic_i16_frame(proto::kSubPosition, l30_comm::filled_i16(50)));

    REQUIRE(wait_until([&] { return calls.load() == 1; }));
    REQUIRE(calls.load() == 1);

    dispatcher.stop();
}

// DstID 不是本机、或 SrcID 不是本节点的帧都应被过滤掉。
TEST_CASE("comm: frames addressed elsewhere are dropped", "[comm][l30][client]") {
    std::atomic<int> calls{0};

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    const auto handler_id =
        client.add_report_handler(proto::kSubPosition, [&calls](const std::vector<uint8_t> &) { ++calls; });
    static_cast<void>(handler_id);

    const auto values = l30_comm::filled_i16(50);
    const auto payload = l30_comm::periodic_i16_payload(values);
    // DstID 5：发给别的主机。
    raw->inject(l30_comm::fd_reply(l30_comm::report_id(proto::kSubPosition, 1, 5), payload));
    // SrcID 7：来自别的节点。
    raw->inject(l30_comm::fd_reply(l30_comm::report_id(proto::kSubPosition, 7, 0), payload));
    raw->inject(l30_comm::periodic_i16_frame(proto::kSubPosition, values));

    REQUIRE(wait_until([&] { return calls.load() == 1; }));
    REQUIRE(calls.load() == 1);

    dispatcher.stop();
}

// 同一个子命令可挂多个回调，两个都应收到；remove 掉一个后另一个继续收。
TEST_CASE("comm: report handlers are added and removed independently", "[comm][l30][client]") {
    std::atomic<int> first{0};
    std::atomic<int> second{0};

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    const auto first_id =
        client.add_report_handler(proto::kSubPosition, [&first](const std::vector<uint8_t> &) { ++first; });
    const auto second_id =
        client.add_report_handler(proto::kSubPosition, [&second](const std::vector<uint8_t> &) { ++second; });
    static_cast<void>(second_id);

    const auto frame = l30_comm::periodic_i16_frame(proto::kSubPosition, l30_comm::filled_i16(50));
    raw->inject(frame);
    REQUIRE(wait_until([&] { return first.load() == 1 && second.load() == 1; }));

    client.remove_report_handler(proto::kSubPosition, first_id);
    raw->inject(frame);
    REQUIRE(wait_until([&] { return second.load() == 2; }));
    REQUIRE(first.load() == 1);
    REQUIRE(second.load() == 2);

    dispatcher.stop();
}

// 周期配置的 ACK 带 Access=1，不是报告：它绝不能进 report 回调。
TEST_CASE("comm: an Access=1 periodic ACK is not a report", "[comm][l30][client]") {
    std::atomic<int> calls{0};

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    const auto handler_id =
        client.add_report_handler(proto::kSubPosition, [&calls](const std::vector<uint8_t> &) { ++calls; });
    static_cast<void>(handler_id);

    REQUIRE(l30_comm::response_id(proto::kParentPeriodic, proto::kSubPosition, proto::kAccessWrite) ==
            kPeriodicConfigAckId);
    raw->inject(l30_comm::fd_reply(kPeriodicConfigAckId, proto::ack_payload()));
    raw->inject(l30_comm::periodic_i16_frame(proto::kSubPosition, l30_comm::filled_i16(50)));

    REQUIRE(wait_until([&] { return calls.load() == 1; }));
    REQUIRE(calls.load() == 1);

    dispatcher.stop();
}

// read_tactile 把 0x10/0x11 两帧重组成 12x6=72 字节矩阵。
TEST_CASE("comm: read_tactile reassembles both response frames", "[comm][l30][client]") {
    const auto tactile_request_id =
        l30_comm::request_id(proto::kParentTactile, proto::kSubTactileThumb, proto::kAccessRead);

    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(tactile_request_id, l30_comm::tactile_handler(l30_comm::tactile_ramp()));

    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    const auto matrix = client.read_tactile(proto::kSubTactileThumb, kTimeout);
    REQUIRE(matrix == l30_comm::tactile_ramp());

    dispatcher.stop();
}

// Python fix#141 回归：触觉读只应和触觉读互斥。让一次触觉读卡在等第二帧上，
// 期间的 enable ACK 必须照样完成，最后补上第二帧让触觉读收尾。
TEST_CASE("comm: an in-flight tactile read does not block other requests", "[comm][l30][client]") {
    std::atomic<bool> tactile_finished{false};
    std::atomic<bool> tactile_matched{false};

    const auto tactile_request_id =
        l30_comm::request_id(proto::kParentTactile, proto::kSubTactileThumb, proto::kAccessRead);
    const auto tactile_response_id =
        l30_comm::response_id(proto::kParentTactile, proto::kSubTactileThumb, proto::kAccessRead);

    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    // Only the first of the two tactile frames, so the read stays in flight.
    raw->on_request(tactile_request_id, [](const Message &req) {
        auto frames = l30_comm::tactile_frames(l30_comm::tactile_ramp(), proto::expected_response_id(req.id));
        frames.pop_back();
        return frames;
    });
    raw->on_request(kEnableRequestId, l30_comm::ack_handler());

    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    std::exception_ptr tactile_error, enable_error;
    std::thread reader([&] {
        try {
            const auto matrix = client.read_tactile(proto::kSubTactileThumb, std::chrono::milliseconds{3000});
            tactile_matched = (matrix == l30_comm::tactile_ramp());
        } catch (...) {
            tactile_error = std::current_exception();
        }
        tactile_finished = true;
    });

    const auto in_flight = wait_until([&] { return l30_comm::count_id(raw, tactile_request_id) == 1; });

    bool enable_acked = false;
    if (in_flight) {
        try {
            const auto ack = client.request_ack(proto::kParentControl, proto::kSubEnable,
                                                proto::empty_request_payload(), proto::kEmptyRequestBytes, kTimeout);
            enable_acked = ack.size() == proto::kAckBytes && ack[proto::kStatusByteIndex] == proto::kStatusOk;
        } catch (...) {
            enable_error = std::current_exception();
        }
    }

    raw->inject(l30_comm::tactile_frames(l30_comm::tactile_ramp(), tactile_response_id).back());
    const auto joined = wait_until([&] { return tactile_finished.load(); }, std::chrono::milliseconds{4000});
    reader.join();
    if (tactile_error) {
        std::rethrow_exception(tactile_error);
    }
    if (enable_error) {
        std::rethrow_exception(enable_error);
    }

    REQUIRE(in_flight);
    REQUIRE(enable_acked);
    REQUIRE(joined);
    REQUIRE(tactile_matched.load());

    dispatcher.stop();
}

// R22 回归：触觉读被设备拒绝时应答的是普通单帧 ACK（BYTE1=kSingleFrameTransaction），
// 不是两帧重组；Client 必须先按 check_response_status 识别它，把原始状态码
// 带出来，而不是报一句通用的“未知触觉事务”。
TEST_CASE("comm: read_tactile preserves the original status from a normal error ACK", "[comm][l30][client]") {
    constexpr uint8_t kTactileRefused = 0x21;

    const auto tactile_request_id =
        l30_comm::request_id(proto::kParentTactile, proto::kSubTactileThumb, proto::kAccessRead);

    auto transport = std::make_unique<FakeTransport>();
    transport->on_request(tactile_request_id, l30_comm::ack_handler(kTactileRefused));

    CANDispatcher dispatcher(std::move(transport));
    Client client(dispatcher, l30_comm::kNodeId, l30_comm::kHostId);

    uint8_t reported = proto::kStatusOk;
    bool threw = false;
    try {
        static_cast<void>(client.read_tactile(proto::kSubTactileThumb, kTimeout));
    } catch (const ProtocolError &error) {
        threw = true;
        reported = error.status();
    }
    REQUIRE(threw);
    REQUIRE(reported == kTactileRefused);

    dispatcher.stop();
}

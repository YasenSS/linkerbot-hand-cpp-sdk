#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include <linkerbot/hand/l20lite/dual_frame.hpp>

using linkerbot::hand::l20lite::DualFrameAssembler;
using linkerbot::hand::l20lite::kAngleFrameA;
using linkerbot::hand::l20lite::kAngleFrameB;
using linkerbot::hand::l20lite::kJointCount;

// DualFrameAssembler：两帧到齐才拼出 10 关节；200ms 内重叠 sense 被丢掉。

// 先收 0x01（6 关节）不够；再收 0x04（4 关节）才拼出 10 关节。
TEST_CASE("comm: dual-frame assembler waits for both frames", "[comm][l20lite][dual_frame]") {
    DualFrameAssembler assembler(kAngleFrameA, kAngleFrameB);
    REQUIRE(assembler.begin_sense(true));

    const std::array<uint8_t, 6> first{1, 2, 3, 4, 5, 6};
    REQUIRE_FALSE(assembler.put(0x01, first.data(), first.size()).has_value());

    const std::array<uint8_t, 4> second{7, 8, 9, 10};
    const auto assembled = assembler.put(0x04, second.data(), second.size());
    REQUIRE(assembled.has_value());
    REQUIRE(*assembled == (std::array<uint8_t, kJointCount>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
}

// force must not supersede a request that is still on the wire.
TEST_CASE("comm: dual-frame assembler drops overlapping begin_sense", "[comm][l20lite][dual_frame]") {
    DualFrameAssembler assembler(kAngleFrameA, kAngleFrameB);
    REQUIRE(assembler.begin_sense());
    REQUIRE_FALSE(assembler.begin_sense());
    REQUIRE_FALSE(assembler.begin_sense(true));
}

// No wire sequence: expiry permanently quarantines the assembler.
TEST_CASE("comm: dual-frame assembler quarantines after timeout", "[comm][l20lite][dual_frame]") {
    DualFrameAssembler assembler(kAngleFrameA, kAngleFrameB);
    REQUIRE(assembler.begin_sense());
    std::this_thread::sleep_for(std::chrono::milliseconds{210});
    REQUIRE_THROWS(assembler.begin_sense());
}

// 非本组装器命令（0x02）被忽略，put 无值。
TEST_CASE("comm: dual-frame assembler ignores unknown cmd", "[comm][l20lite][dual_frame]") {
    DualFrameAssembler assembler(kAngleFrameA, kAngleFrameB);
    const std::array<uint8_t, 6> payload{1, 2, 3, 4, 5, 6};
    REQUIRE_FALSE(assembler.put(0x02, payload.data(), payload.size()).has_value());
}

#include "hand/l20lite/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"
#include <future>
#include <linkerbot/hand/l20lite/l20lite.hpp>

TEST_CASE("comm: L20Lite dual-frame manager enforces reply boundaries", "[comm][l20lite][rfix]") {
    using namespace std::chrono_literals;
    using namespace linkerbot::hand::l20lite;
    using linkerbot::can::Message;
    auto transport = std::make_unique<linkerbot::can::FakeTransport>();
    auto *raw = transport.get();
    L20lite hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    int mode = 0;
    SECTION("conflicting first fragment") { mode = 1; }
    SECTION("extended first fragment") { mode = 2; }
    SECTION("FD first fragment") { mode = 3; }
    SECTION("oversized first fragment") { mode = 4; }
    SECTION("identical first fragment") { mode = 5; }
    raw->on_request(0x28, [mode](const Message &request) {
        auto reply = l20lite_comm::reply_cmd(0x28, request.data[0], request.data[0] == 1 ? 6 : 4, 42);
        if (request.data[0] != 1) {
            return std::vector<Message>{reply};
        }
        if (mode == 1) {
            auto conflict = reply;
            conflict.data[1] = 99;
            return std::vector<Message>{reply, conflict};
        }
        if (mode == 2) {
            reply.is_extended = true;
        }
        if (mode == 3) {
            reply.is_fd = true;
        }
        if (mode == 4) {
            reply.dlc = 65;
        }
        if (mode == 5) {
            return std::vector<Message>{reply, reply};
        }
        return std::vector<Message>{reply};
    });
    if (mode == 5) {
        REQUIRE_NOTHROW(hand.angle.get_blocking(100ms));
    } else {
        REQUIRE_THROWS(hand.angle.get_blocking(25ms));
        REQUIRE_FALSE(hand.angle.get_snapshot());
        const auto count = raw->sent().size();
        REQUIRE_THROWS(hand.angle.get_blocking(25ms));
        REQUIRE(raw->sent().size() == count);
    }
}

namespace {
    class PacedBatchTransport : public linkerbot::can::FakeTransport {
    public:
        std::vector<std::chrono::steady_clock::time_point> sent_at;
        bool prefers_write_batch() const override { return true; }
        void write(const linkerbot::can::Message &frame) override {
            sent_at.push_back(std::chrono::steady_clock::now());
            FakeTransport::write(frame);
        }
        void write_batch(const std::vector<linkerbot::can::Message> &) override {
            throw std::runtime_error("unpaced driver batch would omit required device turnaround");
        }
    };
}
TEST_CASE("comm: L20Lite paced joint commands remain contiguous physical batches", "[comm][l20lite][rfix]") {
    using namespace linkerbot::hand::l20lite;
    auto transport = std::make_unique<PacedBatchTransport>();
    auto *raw = transport.get();
    L20lite hand(Side::kLeft, std::move(transport), "fake", test_util::unique_mapping_path());
    auto a = std::async(std::launch::async,
                        [&] { hand.current_limit.set_current_limits({26, 26, 26, 26, 26, 26, 26, 26, 26, 26}); });
    auto b = std::async(std::launch::async,
                        [&] { hand.speed.set_speeds(Speed::from_array({20, 20, 20, 20, 20, 20, 20, 20, 20, 20})); });
    REQUIRE_NOTHROW(a.get());
    REQUIRE_NOTHROW(b.get());
    const auto sent = raw->sent();
    REQUIRE(sent.size() == 4);
    REQUIRE(sent[1].data[0] == sent[0].data[0] + 1);
    REQUIRE(sent[3].data[0] == sent[2].data[0] + 1);
    REQUIRE(raw->sent_at.size() == 4);
    REQUIRE(raw->sent_at[1] - raw->sent_at[0] >= kVectorInterFrameDelay);
    REQUIRE(raw->sent_at[3] - raw->sent_at[2] >= kVectorInterFrameDelay);
}

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/fake_transport.hpp>
#include <linkerbot/hand/l20/angle.hpp>
#include <linkerbot/hand/l20/angle_manager.hpp>
#include <linkerbot/hand/l20/l20.hpp>
#include <linkerbot/hand/l20/multi_frame.hpp>

#include "hand/l20/comm_helpers.hpp"
#include "hand/support/temp_path.hpp"

using linkerbot::can::CANDispatcher;
using linkerbot::can::FakeTransport;
using linkerbot::hand::l20::Angle;
using linkerbot::hand::l20::AngleManager;
using linkerbot::hand::l20::FiveFrameAssembler;
using linkerbot::hand::l20::kAngleFrames;
using linkerbot::hand::l20::kJointCount;
using linkerbot::hand::l20::L20;
using linkerbot::hand::l20::Side;

namespace {
    std::vector<std::vector<uint8_t>> last_n(FakeTransport *transport, size_t n) {
        const auto sent = l20_comm::payloads(transport);
        REQUIRE(sent.size() >= n);
        return {sent.end() - static_cast<std::ptrdiff_t>(n), sent.end()};
    }
} // namespace

// FiveFrameAssembler：五帧到齐才拼出 16 关节；保留位不写入关节向量。

// 先收 0x41–0x44 不够；再收 0x45 才拼出 16 关节，保留槽被跳过。
TEST_CASE("comm: five-frame assembler waits for all frames", "[comm][l20][multi_frame]") {
    FiveFrameAssembler assembler(kAngleFrames);
    REQUIRE(assembler.begin_sense(true));

    const std::array<uint8_t, 6> thumb{10, 20, 30, 0xAA, 0xBB, 40};
    const std::array<uint8_t, 6> index{50, 0xCC, 60, 0xAA, 0xBB, 70};
    const std::array<uint8_t, 6> middle{80, 0xCC, 90, 0xAA, 0xBB, 100};
    const std::array<uint8_t, 6> ring{110, 0xCC, 120, 0xAA, 0xBB, 130};
    const std::array<uint8_t, 6> pinky{140, 0xCC, 150, 0xAA, 0xBB, 160};

    REQUIRE_FALSE(assembler.put(0x41, thumb.data(), thumb.size()).has_value());
    REQUIRE_FALSE(assembler.put(0x42, index.data(), index.size()).has_value());
    REQUIRE_FALSE(assembler.put(0x43, middle.data(), middle.size()).has_value());
    REQUIRE_FALSE(assembler.put(0x44, ring.data(), ring.size()).has_value());

    const auto assembled = assembler.put(0x45, pinky.data(), pinky.size());
    REQUIRE(assembled.has_value());
    REQUIRE(*assembled ==
            (std::array<uint8_t, kJointCount>{10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160}));
}

// 只收到四帧时 put 返回 nullopt。
TEST_CASE("comm: five-frame assembler incomplete without fifth frame", "[comm][l20][multi_frame]") {
    FiveFrameAssembler assembler(kAngleFrames);
    REQUIRE(assembler.begin_sense(true));

    const std::array<uint8_t, 6> payload{1, 2, 3, 4, 5, 6};
    REQUIRE_FALSE(assembler.put(0x41, payload.data(), payload.size()).has_value());
    REQUIRE_FALSE(assembler.put(0x42, payload.data(), payload.size()).has_value());
    REQUIRE_FALSE(assembler.put(0x43, payload.data(), payload.size()).has_value());
    REQUIRE_FALSE(assembler.put(0x44, payload.data(), payload.size()).has_value());
}

// 在途窗口不可由 force=true 覆盖。
TEST_CASE("comm: five-frame assembler drops overlapping begin_sense", "[comm][l20][multi_frame]") {
    FiveFrameAssembler assembler(kAngleFrames);
    REQUIRE(assembler.begin_sense());
    REQUIRE_FALSE(assembler.begin_sense());
    REQUIRE_FALSE(assembler.begin_sense(true));
}

// 超时后无法区分迟到帧，必须重建 assembler。
TEST_CASE("comm: five-frame assembler quarantines after timeout", "[comm][l20][multi_frame]") {
    FiveFrameAssembler assembler(kAngleFrames);
    REQUIRE(assembler.begin_sense());
    std::this_thread::sleep_for(std::chrono::milliseconds{210});
    REQUIRE_FALSE(assembler.begin_sense());
    REQUIRE(assembler.quarantined());
}

// 非本组装器命令（0x02）被忽略，put 无值。
TEST_CASE("comm: five-frame assembler ignores unknown cmd", "[comm][l20][multi_frame]") {
    FiveFrameAssembler assembler(kAngleFrames);
    const std::array<uint8_t, 6> payload{1, 2, 3, 4, 5, 6};
    REQUIRE_FALSE(assembler.put(0x02, payload.data(), payload.size()).has_value());
}

// set_angles 拆成 0x41–0x45 五帧 DLC=7；保留位填 0；拇指 yaw 在 0x41 BYTE2。
TEST_CASE("comm: set_angles packs five frames with reserved zeros", "[comm][l20][multi_frame]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, 0x41, 0));
    CANDispatcher dispatcher(std::move(transport));

    AngleManager manager(l20_comm::kLeftId, dispatcher, test_util::unique_mapping_path(), "left", "can0");
    manager.start();
    manager.set_angles({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});

    const auto sent = last_n(raw, 5);
    REQUIRE(sent[0] == (std::vector<uint8_t>{0x41, 1, 2, 3, 0, 0, 4}));
    REQUIRE(sent[0][2] == 2);
    REQUIRE(sent[1] == (std::vector<uint8_t>{0x42, 5, 0, 6, 0, 0, 7}));
    REQUIRE(sent[2] == (std::vector<uint8_t>{0x43, 8, 0, 9, 0, 0, 10}));
    REQUIRE(sent[3] == (std::vector<uint8_t>{0x44, 11, 0, 12, 0, 0, 13}));
    REQUIRE(sent[4] == (std::vector<uint8_t>{0x45, 14, 0, 15, 0, 0, 16}));

    manager.stop();
    dispatcher.stop();
}

// L20 set_raw / set_angles：五帧保留位仍为 0，拇指 yaw 在 0x41 data[2]。
TEST_CASE("comm: L20 set_raw and set_angles dump five angle frames", "[comm][l20][multi_frame]") {
    auto transport = std::make_unique<FakeTransport>();
    auto *raw = transport.get();
    raw->on_request(l20_comm::kLeftId, l20_comm::five_handler(l20_comm::kLeftId, 0x41, 0));

    L20 hand(Side::kLeft, std::move(transport), "can0", test_util::unique_mapping_path());

    hand.angle.set_angles({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
    auto sent = last_n(raw, 5);
    REQUIRE(sent[0] == (std::vector<uint8_t>{0x41, 1, 2, 3, 0, 0, 4}));
    REQUIRE(sent[0][2] == 2);

    hand.angle.set_angles_percent(Angle::from_array(
        std::array<float, 16>{100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100}));
    sent = last_n(raw, 5);
    REQUIRE(sent[0] == (std::vector<uint8_t>{0x41, 255, 255, 255, 0, 0, 255}));
    REQUIRE(sent[0][2] == 255);
    REQUIRE(sent[1] == (std::vector<uint8_t>{0x42, 255, 0, 255, 0, 0, 255}));
    REQUIRE(sent[2] == (std::vector<uint8_t>{0x43, 255, 0, 255, 0, 0, 255}));
    REQUIRE(sent[3] == (std::vector<uint8_t>{0x44, 255, 0, 255, 0, 0, 255}));
    REQUIRE(sent[4] == (std::vector<uint8_t>{0x45, 255, 0, 255, 0, 0, 255}));
}

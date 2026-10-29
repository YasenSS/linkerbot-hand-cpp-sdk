#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <sstream>
#include <stdexcept>

#include "hand/support/interactive_session.hpp"

namespace {
    // Script the operator without opening CAN or moving hardware.
    struct Console {
        std::istringstream input;
        std::ostringstream output;
        std::streambuf *old_input;
        std::streambuf *old_output;
        explicit Console(const std::string &script)
            : input(script), old_input(std::cin.rdbuf(input.rdbuf())), old_output(std::cout.rdbuf(output.rdbuf())) {}
        ~Console() {
            std::cin.rdbuf(old_input);
            std::cout.rdbuf(old_output);
        }
    };
}

TEST_CASE("comm: O30 guided interaction requires readiness and explicit verdict", "[comm][o30][angle][interaction]") {
    Console console("invalid\n\n\ninvalid\ny\n\ny\n");
    int moves = 0;
    test_util::InteractiveSession session("中文引导");
    session.step(
        "食指侧摆",
        [&] {
            REQUIRE(console.output.str().find("准备好后按回车执行") != std::string::npos);
            ++moves;
        },
        "向远离拇指的方向侧摆");
    session.step("食指返回", [&] { ++moves; }, "向靠近拇指的方向侧摆");
    session.run(true);
    REQUIRE(moves == 2);
    REQUIRE_FALSE(session.quit_early());
    REQUIRE(session.failed_steps().empty());
    REQUIRE(console.output.str().find("回车不会判定通过") != std::string::npos);
}

TEST_CASE("comm: O30 guided cancellation before movement never runs an action", "[comm][o30][angle][interaction]") {
    const auto input = GENERATE("q\n", "", "invalid\nq\n");
    Console console(input);
    int moves = 0;
    test_util::InteractiveSession session("取消");
    session.step("横滚", [&] { ++moves; }, "垂直掌心");
    session.step("返回", [&] { ++moves; }, "平行掌心");
    session.run(true);
    REQUIRE(moves == 0);
    REQUIRE(session.quit_early());
    REQUIRE(session.failed_steps().size() == 1);
}

TEST_CASE("comm: O30 failed skipped quit and EOF verdicts prevent further movement",
          "[comm][o30][angle][interaction]") {
    const auto verdict = GENERATE("n\n", "s\n", "q\n", "");
    Console console(std::string("\n") + verdict);
    int moves = 0;
    test_util::InteractiveSession session("停止");
    session.step("旋转", [&] { ++moves; }, "靠近食指");
    session.step("返回", [&] { ++moves; }, "远离食指");
    session.run(true);
    REQUIRE(moves == 1);
    REQUIRE(session.failed_steps().size() == 1);
    REQUIRE_FALSE(session.failed_steps().front().passed);
}

TEST_CASE("comm: O30 interaction records hardware failure before propagating it", "[comm][o30][angle][interaction]") {
    Console console("\n");
    int subsequent_moves = 0;
    test_util::InteractiveSession session("硬件错误");
    session.step("侧摆", [] { throw std::runtime_error("故障"); }, "仅食指侧摆");
    session.step("返回", [&] { ++subsequent_moves; }, "返回");
    REQUIRE_THROWS_AS(session.run(true), std::runtime_error);
    REQUIRE(subsequent_moves == 0);
    REQUIRE(session.failed_steps().size() == 1);
}

TEST_CASE("comm: existing unguided sessions retain their input sequence", "[comm][o30][angle][interaction]") {
    Console console("n\ny\n");
    int moves = 0;
    test_util::InteractiveSession session("原有交互");
    session.step("第一步", [&] { ++moves; }, "观察");
    session.step("第二步", [&] { ++moves; }, "观察");
    session.run();
    REQUIRE(moves == 2);
    REQUIRE(session.failed_steps().size() == 1);
    REQUIRE(console.output.str().find("准备好后按回车执行") == std::string::npos);
}

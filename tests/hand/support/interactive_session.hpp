#pragma once

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace test_util {

    struct StepResult {
        std::string instruction;
        std::string expected;
        bool passed = false;
        bool skipped = false;
    };

    class InteractiveSession {
    public:
        explicit InteractiveSession(std::string test_name) : test_name_(std::move(test_name)) {}

        InteractiveSession &step(const std::string &instruction, std::function<void()> action,
                                 const std::string &expected) {
            steps_.push_back({instruction, std::move(action), expected});
            return *this;
        }

        // Guided mode is opt-in for hardware tests that must wait before each
        // action and must not continue after a failed/incomplete observation.
        void run(bool guided = false) {
            guided_ = guided;
            std::cout << "\n=== 交互测试: " << test_name_ << " ===\n";
            std::cout << "步骤数: " << steps_.size() << "\n\n";

            for (size_t i = 0; i < steps_.size(); ++i) {
                if (quit_early_) {
                    break;
                }

                const auto &s = steps_[i];
                std::cout << "步骤 " << (i + 1) << "/" << steps_.size() << ": " << s.instruction << "\n";
                std::cout << "期望: " << s.expected << "\n" << std::flush;

                if (guided) {
                    std::string ready;
                    while (true) {
                        std::cout << "准备好后按回车执行；输入 q 结束本轮测试: " << std::flush;
                        if (!std::getline(std::cin, ready) || ready == "q" || ready == "quit") {
                            quit_early_ = true;
                            results_.push_back({s.instruction, s.expected, false, true});
                            std::cout << "  -> 已取消，未执行本步骤\n";
                            break;
                        }
                        if (ready.empty()) {
                            break;
                        }
                        std::cout << "请输入回车或 q。\n";
                    }
                    if (quit_early_) {
                        break;
                    }
                }

                try {
                    s.action();
                } catch (...) {
                    results_.push_back({s.instruction, s.expected, false, false});
                    throw;
                }

                std::string input;
                while (true) {
                    std::cout << "结果? [y]通过 / [n]失败 / [s]跳过 / [q]退出: " << std::flush;
                    if (!std::getline(std::cin, input)) {
                        input = "q";
                        break;
                    }
                    if (!guided || input == "y" || input == "yes" || input == "n" || input == "no" || input == "s" ||
                        input == "skip" || input == "q" || input == "quit") {
                        break;
                    }
                    std::cout << "请输入 y、n、s 或 q；回车不会判定通过。\n";
                }

                StepResult result;
                result.instruction = s.instruction;
                result.expected = s.expected;

                if (input == "y" || input == "yes") {
                    result.passed = true;
                    std::cout << "  -> 通过\n\n";
                } else if (input == "s" || input == "skip") {
                    result.skipped = true;
                    std::cout << "  -> 已跳过\n\n";
                } else if (input == "q" || input == "quit") {
                    result.skipped = true;
                    quit_early_ = true;
                    std::cout << "  -> 已退出\n\n";
                } else {
                    result.passed = false;
                    std::cout << "  -> 失败\n\n";
                }

                results_.push_back(result);
                if (guided && !result.passed) {
                    std::cout << "本轮未通过，停止后续步骤。\n";
                    break;
                }
            }
        }

        void save_report(const std::string &report_dir = "tests/reports") {
            std::filesystem::create_directories(report_dir);

            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm_buf{};
#ifdef _WIN32
            localtime_s(&tm_buf, &time); // NOLINT
#else
            localtime_r(&time, &tm_buf); // NOLINT
#endif

            std::ostringstream filename;
            filename << report_dir << "/" << test_name_ << "_" << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".json";

            std::ofstream file(filename.str());
            if (!file.is_open()) {
                std::cerr << "警告: 无法写入报告 " << filename.str() << "\n";
                return;
            }

            file << "{\n";
            file << R"(  "test_name": ")" << escape_json(test_name_) << "\",\n";
            file << R"(  "timestamp": ")" << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S") << "\",\n";
            file << "  \"steps\": [\n";

            for (size_t i = 0; i < results_.size(); ++i) {
                const auto &r = results_[i];
                file << "    {\n";
                file << R"(      "instruction": ")" << escape_json(r.instruction) << "\",\n";
                file << R"(      "expected": ")" << escape_json(r.expected) << "\",\n";
                file << "      \"passed\": " << (r.passed ? "true" : "false") << ",\n";
                file << "      \"skipped\": " << (r.skipped ? "true" : "false") << "\n";
                file << "    }";
                if (i + 1 < results_.size()) {
                    file << ",";
                }
                file << "\n";
            }

            file << "  ]\n";
            file << "}\n";

            std::cout << "报告已保存: " << filename.str() << "\n";
        }

        bool quit_early() const { return quit_early_; }

        std::vector<StepResult> failed_steps() const {
            std::vector<StepResult> failed;
            for (const auto &r : results_) {
                if (!r.passed && (guided_ || !r.skipped)) {
                    failed.push_back(r);
                }
            }
            return failed;
        }

    private:
        struct Step {
            std::string instruction;
            std::function<void()> action;
            std::string expected;
        };

        static std::string escape_json(const std::string &s) {
            std::string result;
            result.reserve(s.size());
            for (char c : s) {
                switch (c) {
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    result += c;
                }
            }
            return result;
        }

        std::string test_name_;
        std::vector<Step> steps_;
        std::vector<StepResult> results_;
        bool quit_early_ = false;
        bool guided_ = false;
    };

} // namespace test_util

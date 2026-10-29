#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace test_util {

    inline std::filesystem::path unique_mapping_path() {
        auto dir = std::filesystem::temp_directory_path() / "linkerbot-hand-cpp";
        std::filesystem::create_directories(dir);
        static std::atomic<std::uint64_t> seq{0};
        const auto n = seq.fetch_add(1);
        const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        return dir / ("mapping-" + std::to_string(ts) + "-" + std::to_string(n) + ".toml");
    }

} // namespace test_util

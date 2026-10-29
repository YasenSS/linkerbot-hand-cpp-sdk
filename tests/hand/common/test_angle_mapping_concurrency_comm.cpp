#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <linkerbot/hand/angle_mapping.hpp>

#include "hand/support/temp_path.hpp"

using linkerbot::hand::AngleMapping;
using linkerbot::hand::AngleMappingManager;
using linkerbot::hand::default_angle_mapping;

namespace {

    AngleMapping reverse_mapping(std::size_t joint_count) {
        auto mapping = default_angle_mapping(joint_count);
        for (auto &row : mapping) {
            std::reverse(row.begin(), row.end());
        }
        return mapping;
    }

    // A bare filename (no directory component) exercises the empty
    // parent_path() edge case that a relative std::filesystem::path can hit
    // before it is normalized to an absolute path.
    std::filesystem::path unique_bare_relative_path() {
        static std::atomic<std::uint64_t> seq{0};
        const auto n = seq.fetch_add(1);
        const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::path("linkerbot-hand-cpp-bare-mapping-" + std::to_string(ts) + "-" + std::to_string(n) +
                                     ".toml");
    }

    std::filesystem::path unique_relative_dir_path() {
        static std::atomic<std::uint64_t> seq{0};
        const auto n = seq.fetch_add(1);
        const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::path("linkerbot-hand-cpp-relative-test-" + std::to_string(ts) + "-" +
                                     std::to_string(n)) /
               "mapping.toml";
    }

    // Removes a mapping file and the lock file persist()/load_or_create()
    // leave beside it, resolving both relative to the current working
    // directory exactly like AngleMappingManager does.
    struct RelativePathCleanup {
        std::filesystem::path mapping_path;
        ~RelativePathCleanup() {
            std::error_code ec;
            std::filesystem::remove(mapping_path, ec);
            std::filesystem::remove(std::filesystem::path(mapping_path.string() + ".lock"), ec);
            if (mapping_path.has_parent_path()) {
                std::filesystem::remove_all(mapping_path.parent_path(), ec);
            }
        }
    };

} // namespace

// R17 回归：同一个 AngleMappingManager 实例上并发 map_values / get_mapping /
// set_mapping 不应该出现半写的行或崩溃——这些调用共享同一份内存映射数据。
TEST_CASE("comm: concurrent map_values/get_mapping/set_mapping do not race", "[comm][angle_mapping][concurrency]") {
    AngleMappingManager manager("l6", {"a", "b"}, test_util::unique_mapping_path());
    const auto identity = default_angle_mapping(2);
    const auto reversed = reverse_mapping(2);

    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};

    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&]() {
            while (!stop.load()) {
                try {
                    const auto mapped = manager.map_values({0, 128});
                    const bool is_identity = mapped[0] == 0 && mapped[1] == 128;
                    const bool is_reversed = mapped[0] == 255 && mapped[1] == 127;
                    if (!is_identity && !is_reversed) {
                        failed.store(true);
                    }
                    const auto snapshot = manager.get_mapping();
                    if (snapshot != identity && snapshot != reversed) {
                        failed.store(true);
                    }
                } catch (...) {
                    failed.store(true);
                }
            }
        });
    }

    std::thread writer([&]() {
        for (int i = 0; i < 200; ++i) {
            manager.set_mapping(i % 2 == 0 ? reversed : identity);
        }
    });

    writer.join();
    stop.store(true);
    for (auto &reader : readers) {
        reader.join();
    }

    REQUIRE_FALSE(failed.load());
}

// R17 回归：两个identity（不同 model/side/interface）共享同一个文件，
// 各自并发写自己的 section 时，谁也不能把另一方的 section 冲掉。
TEST_CASE("comm: concurrent writers for two identities preserve both sections", "[comm][angle_mapping][concurrency]") {
    const auto path = test_util::unique_mapping_path();
    AngleMappingManager l6("l6", {"a", "b"}, path);
    AngleMappingManager l20lite("l20lite", {"c", "d", "e"}, path);

    const auto l6_custom = reverse_mapping(2);
    const auto l20lite_custom = reverse_mapping(3);

    std::thread writer_l6([&]() {
        for (int i = 0; i < 50; ++i) {
            l6.set_mapping(l6_custom);
        }
    });
    std::thread writer_l20lite([&]() {
        for (int i = 0; i < 50; ++i) {
            l20lite.set_mapping(l20lite_custom);
        }
    });
    writer_l6.join();
    writer_l20lite.join();

    AngleMappingManager reloaded_l6("l6", {"a", "b"}, path);
    AngleMappingManager reloaded_l20lite("l20lite", {"c", "d", "e"}, path);

    REQUIRE(reloaded_l6.get_mapping() == l6_custom);
    REQUIRE(reloaded_l20lite.get_mapping() == l20lite_custom);
}

// R17 回归：两个 identity 通过符号链接别名和真实路径分别访问同一份底层文件时，
// 也必须解析到同一个规范路径并共用同一把锁——否则两个写者会各自锁住不同的
// lock 文件，互相看不见对方，产生和"没有锁"一样的竞态。若当前文件系统/权限
// 不支持创建符号链接，则优雅跳过而不是报错。
TEST_CASE("comm: concurrent writers via a symlink alias preserve both sections", "[comm][angle_mapping][concurrency]") {
    const auto real_path = test_util::unique_mapping_path();
    const auto alias_path = real_path.parent_path() / (real_path.filename().string() + ".alias.toml");

    std::error_code symlink_ec;
    std::filesystem::create_symlink(real_path.filename(), alias_path, symlink_ec);
    if (symlink_ec) {
        SKIP("symlinks are not supported/permitted here: " + symlink_ec.message());
    }
    struct AliasCleanup {
        std::filesystem::path alias;
        ~AliasCleanup() {
            std::error_code ec;
            std::filesystem::remove(alias, ec);
            std::filesystem::remove(std::filesystem::path(alias.string() + ".lock"), ec);
        }
    } alias_cleanup{alias_path};

    AngleMappingManager via_real("l6", {"a", "b"}, real_path);
    AngleMappingManager via_alias("l20lite", {"c", "d", "e"}, alias_path);

    const auto real_custom = reverse_mapping(2);
    const auto alias_custom = reverse_mapping(3);

    std::thread writer_real([&]() {
        for (int i = 0; i < 50; ++i) {
            via_real.set_mapping(real_custom);
        }
    });
    std::thread writer_alias([&]() {
        for (int i = 0; i < 50; ++i) {
            via_alias.set_mapping(alias_custom);
        }
    });
    writer_real.join();
    writer_alias.join();

    AngleMappingManager reloaded_real("l6", {"a", "b"}, real_path);
    AngleMappingManager reloaded_alias("l20lite", {"c", "d", "e"}, alias_path);

    REQUIRE(reloaded_real.get_mapping() == real_custom);
    REQUIRE(reloaded_alias.get_mapping() == alias_custom);
}

// R17 回归：相对路径（带子目录）应该相对当前工作目录正常创建、写入、重新加载。
TEST_CASE("comm: mapping manager supports a relative output path", "[comm][angle_mapping]") {
    const auto relative_path = unique_relative_dir_path();
    const RelativePathCleanup cleanup{relative_path};

    AngleMappingManager manager("l6", {"a", "b"}, relative_path);
    const auto custom = reverse_mapping(2);
    manager.set_mapping(custom);

    REQUIRE(std::filesystem::exists(relative_path));

    AngleMappingManager reloaded("l6", {"a", "b"}, relative_path);
    REQUIRE(reloaded.get_mapping() == custom);
}

// R17 回归：不带任何目录部分的裸文件名（parent_path() 为空）不应该在
// create_directories 或临时文件命名上崩溃或抛异常。
TEST_CASE("comm: mapping manager supports a bare relative filename", "[comm][angle_mapping]") {
    const auto relative_path = unique_bare_relative_path();
    REQUIRE_FALSE(relative_path.has_parent_path());
    const RelativePathCleanup cleanup{relative_path};

    AngleMappingManager manager("l6", {"a", "b"}, relative_path);
    const auto custom = reverse_mapping(2);
    REQUIRE_NOTHROW(manager.set_mapping(custom));
    REQUIRE(std::filesystem::exists(relative_path));

    AngleMappingManager reloaded("l6", {"a", "b"}, relative_path);
    REQUIRE(reloaded.get_mapping() == custom);
}

// R17 回归：最终 rename 失败时（这里用把落点占成目录来强制触发），必须干净地
// 抛出异常，唯一命名的临时文件不能遗留，原文件位置也不能被破坏。
TEST_CASE("comm: a persist failure leaves the original mapping path untouched", "[comm][angle_mapping]") {
    const auto path = test_util::unique_mapping_path();
    AngleMappingManager manager("l6", {"a", "b"}, path);
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::is_regular_file(path));

    // Replace the mapping file with a directory so the final
    // std::filesystem::rename(tmp, path) in atomic_write() fails with
    // EISDIR, after the temp file has already been fully written.
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);

    REQUIRE_THROWS(manager.set_mapping(reverse_mapping(2)));

    REQUIRE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::is_directory(path));
    REQUIRE(std::filesystem::is_empty(path));

    // No leftover mkstemp() temp file beside the (now-directory) path. The
    // cross-process lock file (prefix + ".lock") is expected to persist
    // across calls by design, so it is excluded from the leak check.
    std::size_t leftover_count = 0;
    const auto prefix = path.filename().string();
    const auto lock_name = prefix + ".lock";
    for (const auto &entry : std::filesystem::directory_iterator(path.parent_path())) {
        const auto name = entry.path().filename().string();
        if (name != prefix && name != lock_name && name.rfind(prefix, 0) == 0) {
            ++leftover_count;
        }
    }
    REQUIRE(leftover_count == 0);
}

#include "hand/support/temp_path.hpp"
#include <catch2/catch_test_macros.hpp>
#include <linkerbot/hand/angle_mapping.hpp>
#include <linkerbot/hand/errors.hpp>

#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
    using Path = std::filesystem::path;

    class MappingEnvironment {
        std::map<std::string, std::optional<Path>> saved_;

        static std::optional<Path> read(const std::string &name) {
#ifdef _WIN32
            const std::wstring key(name.begin(), name.end());
            const auto count = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
            if (!count) {
                return std::nullopt;
            }
            std::wstring value(count, L'\0');
            const auto copied = GetEnvironmentVariableW(key.c_str(), value.data(), count);
            value.resize(copied);
            return Path(value);
#else
            const auto *value = std::getenv(name.c_str());
            return value ? std::optional<Path>{Path(value)} : std::nullopt;
#endif
        }

        static bool write(const std::string &name, const std::optional<Path> &value) {
#ifdef _WIN32
            const std::wstring key(name.begin(), name.end());
            return SetEnvironmentVariableW(key.c_str(), value ? value->c_str() : nullptr) != 0;
#else
            return (value ? ::setenv(name.c_str(), value->c_str(), 1) : ::unsetenv(name.c_str())) == 0;
#endif
        }

    public:
        MappingEnvironment() {
            for (const auto *name : {"XDG_CONFIG_HOME", "HOME", "USERPROFILE", "LOCALAPPDATA", "APPDATA"}) {
                saved_.emplace(name, read(name));
            }
            for (const auto &[name, value] : saved_) {
                set(name, std::nullopt);
            }
        }
        ~MappingEnvironment() {
            for (const auto &[name, value] : saved_) {
                (void)write(name, value);
            }
        }
        void set(const std::string &name, const std::optional<Path> &value) { REQUIRE(write(name, value)); }
    };

    Path mapping_file(const Path &base) { return base / "linkerbot" / "hand_angle_mappings.toml"; }
}

TEST_CASE("comm: explicit angle mapping path needs no home or config environment",
          "[comm][angle_mapping][mapping_paths]") {
    MappingEnvironment environment;
    const auto path = test_util::unique_mapping_path();
    linkerbot::hand::AngleMappingManager manager("l6", {"joint"}, path);
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(manager.path() == std::filesystem::weakly_canonical(path));
    REQUIRE(manager.get_mapping() == linkerbot::hand::default_angle_mapping(1));
}

#ifdef _WIN32
TEST_CASE("comm: Windows default angle mapping path works without HOME", "[comm][angle_mapping][mapping_paths]") {
    MappingEnvironment environment;
    const Path local = L"C:/用户/测试/Local";
    const Path roaming = L"C:/用户/测试/Roaming";
    environment.set("LOCALAPPDATA", local);
    environment.set("APPDATA", roaming);
    environment.set("USERPROFILE", Path(L"C:/用户/测试"));
    REQUIRE(linkerbot::hand::default_angle_mapping_path() == mapping_file(local));
    environment.set("LOCALAPPDATA", std::nullopt);
    REQUIRE(linkerbot::hand::default_angle_mapping_path() == mapping_file(roaming));
    environment.set("APPDATA", std::nullopt);
    REQUIRE(linkerbot::hand::default_angle_mapping_path() == mapping_file(Path(L"C:/用户/测试/.config")));
    environment.set("USERPROFILE", std::nullopt);
    REQUIRE_THROWS_AS(linkerbot::hand::default_angle_mapping_path(), linkerbot::hand::TransportError);
}

TEST_CASE("comm: Windows mapping environment preserves Unicode and tilde paths",
          "[comm][angle_mapping][mapping_paths]") {
    MappingEnvironment environment;
    const Path profile = L"C:/用户/测试";
    const Path xdg = L"C:/配置/角度";
    environment.set("USERPROFILE", profile);
    environment.set("LOCALAPPDATA", Path(L"C:/unused"));
    environment.set("XDG_CONFIG_HOME", xdg);
    REQUIRE(linkerbot::hand::default_angle_mapping_path() == mapping_file(xdg));
    environment.set("XDG_CONFIG_HOME", Path(L"~/配置"));
    REQUIRE(linkerbot::hand::default_angle_mapping_path() == mapping_file(profile / L"配置"));
    environment.set("XDG_CONFIG_HOME", Path(L"~\\配置"));
    REQUIRE(linkerbot::hand::default_angle_mapping_path() == mapping_file(profile / L"配置"));
    environment.set("HOME", Path(L"C:/explicit-home"));
    REQUIRE(linkerbot::hand::default_angle_mapping_path() == mapping_file(Path(L"C:/explicit-home") / L"配置"));
}

TEST_CASE("comm: Windows default mapping creates its file in a Unicode config directory",
          "[comm][angle_mapping][mapping_paths]") {
    MappingEnvironment environment;
    const auto local = test_util::unique_mapping_path().parent_path() / L"配置目录";
    environment.set("LOCALAPPDATA", local);
    linkerbot::hand::AngleMappingManager manager("l6", {"joint"});
    REQUIRE(manager.path() == std::filesystem::weakly_canonical(mapping_file(local)));
    REQUIRE(std::filesystem::exists(manager.path()));
}
#endif

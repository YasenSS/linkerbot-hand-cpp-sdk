#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace linkerbot::hand {

    constexpr std::size_t kAngleMappingColumns = 256;

    using AngleMappingRow = std::array<std::uint8_t, kAngleMappingColumns>;
    using AngleMapping = std::vector<AngleMappingRow>;

    [[nodiscard]] AngleMapping default_angle_mapping(std::size_t joint_count);
    // XDG_CONFIG_HOME overrides the platform default. Windows uses LOCALAPPDATA,
    // then APPDATA, then HOME/USERPROFILE + .config; POSIX uses HOME + .config.
    [[nodiscard]] std::filesystem::path default_angle_mapping_path();

    class AngleMappingManager {
    public:
        AngleMappingManager(std::string model, std::vector<std::string> joint_names,
                            const std::optional<std::filesystem::path> &mapping_path = {}, std::string side = "unknown",
                            std::string interface_name = "unknown");

        [[nodiscard]] const std::filesystem::path &path() const;

        [[nodiscard]] std::vector<std::uint8_t> map_values(const std::vector<std::uint8_t> &raw_values) const;

        [[nodiscard]] AngleMapping get_mapping() const;
        [[nodiscard]] AngleMapping get_default_mapping() const;

        void set_mapping(const AngleMapping &mapping);
        void reset_mapping();

    private:
        AngleMapping load_or_create();
        void persist(const AngleMapping &mapping);
        [[nodiscard]] std::string section_key() const;

        std::string model_;
        std::string side_;
        std::string interface_name_;
        std::vector<std::string> joint_names_;
        std::filesystem::path path_;

        // Guards mapping_ against concurrent map_values()/get_mapping()/
        // set_mapping() calls on this instance. Disk read-modify-write
        // serialization (including across processes and other instances
        // sharing the same file) is handled separately in persist()/
        // load_or_create() via a POSIX file lock keyed on the normalized path.
        mutable std::shared_mutex mapping_mutex_;
        AngleMapping mapping_;
    };

} // namespace linkerbot::hand

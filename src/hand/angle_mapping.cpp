#include "linkerbot/hand/angle_mapping.hpp"

#include <linkerbot/hand/errors.hpp>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#else
#error "AngleMappingManager needs a file-locking implementation for this platform \
(only _WIN32 and POSIX/__APPLE__ are supported)"
#endif

namespace linkerbot::hand {
    namespace {
        struct TomlTable {
            std::string key;
            std::string header;
            std::string body;
        };

        struct TomlDocument {
            std::string preamble;
            std::vector<TomlTable> tables;
        };

        class Cursor {
        public:
            explicit Cursor(std::string_view text) : text_(text) {}

            [[nodiscard]] bool done() const { return pos_ >= text_.size(); }
            [[nodiscard]] std::string_view remaining() const { return text_.substr(pos_); }

            void skip_ws_and_comments() {
                while (!done()) {
                    const char ch = text_[pos_];
                    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                        ++pos_;
                        continue;
                    }
                    if (ch == '#') {
                        while (!done() && text_[pos_] != '\n') {
                            ++pos_;
                        }
                        continue;
                    }
                    break;
                }
            }

            bool eat(char expected) {
                if (!done() && text_[pos_] == expected) {
                    ++pos_;
                    return true;
                }
                return false;
            }

            std::string read_ident() {
                skip_ws_and_comments();
                const auto start = pos_;
                while (!done()) {
                    const char ch = text_[pos_];
                    if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_') {
                        break;
                    }
                    ++pos_;
                }
                return std::string(text_.substr(start, pos_ - start));
            }

            std::string parse_string() {
                skip_ws_and_comments();
                if (!eat('"')) {
                    throw ValidationError("expected TOML string");
                }
                std::string out;
                while (!done()) {
                    const char ch = text_[pos_++];
                    if (ch == '"') {
                        return out;
                    }
                    if (ch == '\\') {
                        if (done()) {
                            throw ValidationError("unterminated TOML escape");
                        }
                        out.push_back(text_[pos_++]);
                    } else {
                        out.push_back(ch);
                    }
                }
                throw ValidationError("unterminated TOML string");
            }

            int parse_int() {
                skip_ws_and_comments();
                const auto start = pos_;
                if (!done() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                    ++pos_;
                }
                if (done() || std::isdigit(static_cast<unsigned char>(text_[pos_])) == 0) {
                    throw ValidationError("expected TOML integer");
                }
                while (!done() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
                    ++pos_;
                }
                if (!done() && text_[pos_] == '.') {
                    throw ValidationError("TOML integer must not be a float");
                }
                return std::stoi(std::string(text_.substr(start, pos_ - start)));
            }

            std::vector<std::string> parse_string_array() {
                skip_ws_and_comments();
                if (!eat('[')) {
                    throw ValidationError("expected TOML array");
                }
                std::vector<std::string> values;
                while (true) {
                    skip_ws_and_comments();
                    if (eat(']')) {
                        return values;
                    }
                    values.push_back(parse_string());
                    skip_ws_and_comments();
                    if (eat(',')) {
                        continue;
                    }
                    skip_ws_and_comments();
                    if (!eat(']')) {
                        throw ValidationError("expected ',' or ']' in string array");
                    }
                    return values;
                }
            }

            std::vector<int> parse_int_array() {
                skip_ws_and_comments();
                if (!eat('[')) {
                    throw ValidationError("expected TOML array");
                }
                std::vector<int> values;
                while (true) {
                    skip_ws_and_comments();
                    if (eat(']')) {
                        return values;
                    }
                    values.push_back(parse_int());
                    skip_ws_and_comments();
                    if (eat(',')) {
                        continue;
                    }
                    skip_ws_and_comments();
                    if (!eat(']')) {
                        throw ValidationError("expected ',' or ']' in integer array");
                    }
                    return values;
                }
            }

            std::vector<std::vector<int>> parse_int_matrix() {
                skip_ws_and_comments();
                if (!eat('[')) {
                    throw ValidationError("expected TOML array");
                }
                std::vector<std::vector<int>> rows;
                while (true) {
                    skip_ws_and_comments();
                    if (eat(']')) {
                        return rows;
                    }
                    rows.push_back(parse_int_array());
                    skip_ws_and_comments();
                    if (eat(',')) {
                        continue;
                    }
                    skip_ws_and_comments();
                    if (!eat(']')) {
                        throw ValidationError("expected ',' or ']' in mapping array");
                    }
                    return rows;
                }
            }

            void skip_value() {
                skip_ws_and_comments();
                if (done()) {
                    return;
                }
                const char ch = remaining().front();
                if (ch == '"') {
                    parse_string();
                    return;
                }
                if (ch != '[') {
                    parse_int();
                    return;
                }
                Cursor probe(remaining());
                probe.eat('[');
                probe.skip_ws_and_comments();
                if (!probe.done() && probe.remaining().front() == '[') {
                    parse_int_matrix();
                } else if (!probe.done() && probe.remaining().front() == '"') {
                    parse_string_array();
                } else {
                    parse_int_array();
                }
            }

        private:
            std::string_view text_;
            std::size_t pos_{0};
        };

        std::optional<std::filesystem::path> environment_path(const char *name) {
#if defined(_WIN32)
            // Configuration directories may contain characters outside the ACP.
            const std::wstring key(name, name + std::strlen(name));
            const auto count = ::GetEnvironmentVariableW(key.c_str(), nullptr, 0);
            if (count == 0) {
                return std::nullopt;
            }
            std::wstring value(count, L'\0');
            const auto copied = ::GetEnvironmentVariableW(key.c_str(), value.data(), count);
            if (copied == 0 || copied >= count) {
                throw TransportError("Configuration environment changed while reading " + std::string(name));
            }
            value.resize(copied);
            return std::filesystem::path(value);
#else
            const auto *value = std::getenv(name);
            if (value == nullptr || value[0] == '\0') {
                return std::nullopt;
            }
            return std::filesystem::path(value);
#endif
        }

        std::filesystem::path home_directory() {
            if (const auto home = environment_path("HOME")) {
                return *home;
            }
#if defined(_WIN32)
            if (const auto profile = environment_path("USERPROFILE")) {
                return *profile;
            }
            throw TransportError("Neither HOME nor USERPROFILE is set");
#else
            throw TransportError("HOME is not set");
#endif
        }

        std::filesystem::path expand_user(std::filesystem::path path) {
            const auto &text = path.native();
            if (text.empty() || text.front() != '~') {
                return path;
            }
            if (text.size() == 1) {
                return home_directory();
            }
            if (text[1] == '/'
#if defined(_WIN32)
                || text[1] == '\\'
#endif
            ) {
                return home_directory() / text.substr(2);
            }
            return path;
        }

        std::string escape_toml_string(std::string_view value) {
            std::string out;
            out.reserve(value.size());
            for (char ch : value) {
                if (ch == '\\' || ch == '"') {
                    out.push_back('\\');
                }
                out.push_back(ch);
            }
            return out;
        }

        struct ParsedTableHeader {
            std::string key;
            std::string header;
        };
        std::optional<ParsedTableHeader> parse_table_header(std::string_view line) {
            std::size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
                ++i;
            }
            if (i >= line.size() || line[i] != '[') {
                return std::nullopt;
            }
            ++i;
            if (i >= line.size()) {
                return std::nullopt;
            }
            const char quote = line[i];
            if (quote != '"' && quote != '\'') {
                return std::nullopt;
            }
            ++i;
            const auto key_start = i;
            while (i < line.size() && line[i] != quote) {
                ++i;
            }
            if (i >= line.size() || i + 1 >= line.size() || line[i + 1] != ']') {
                return std::nullopt;
            }
            return ParsedTableHeader{std::string(line.substr(key_start, i - key_start)),
                                     std::string(line.substr(0, i + 2))};
        }

        TomlDocument parse_document(const std::string &text) {
            TomlDocument doc;
            std::istringstream stream(text);
            std::string line;
            std::ostringstream preamble;
            std::ostringstream body;
            TomlTable current;
            bool in_table = false;

            auto flush_table = [&]() {
                if (in_table) {
                    current.body = body.str();
                    doc.tables.push_back(std::move(current));
                    body.str("");
                    body.clear();
                    current = {};
                }
            };

            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (auto header = parse_table_header(line)) {
                    flush_table();
                    in_table = true;
                    current.key = std::move(header->key);
                    current.header = std::move(header->header);
                    continue;
                }
                if (in_table) {
                    body << line << '\n';
                } else {
                    preamble << line << '\n';
                }
            }
            flush_table();
            doc.preamble = preamble.str();
            return doc;
        }

        std::string render_document(const TomlDocument &doc) {
            std::ostringstream out;
            out << doc.preamble;
            if (!doc.preamble.empty() && doc.preamble.back() != '\n') {
                out << '\n';
            }
            for (const auto &table : doc.tables) {
                out << table.header << '\n';
                out << table.body;
                if (!table.body.empty() && table.body.back() != '\n') {
                    out << '\n';
                }
            }
            return out.str();
        }

        std::string load_file(const std::filesystem::path &path) {
            if (!std::filesystem::exists(path)) {
                return "version = 1\n";
            }
            std::ifstream in(path);
            if (!in) {
                throw TransportError("Failed to read angle mapping TOML: " + path.string());
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }

        // Resolves `path` to an absolute, symlink-resolved form exactly once so
        // a relative/aliased input still identifies a single, well-defined
        // file/lock pair for the manager's whole lifetime, regardless of later
        // process cwd changes. weakly_canonical() resolves any existing
        // components (following symlinks) and lexically normalizes the rest.
        std::filesystem::path resolve_path(const std::filesystem::path &path) {
            std::error_code ec;
            auto resolved = std::filesystem::weakly_canonical(std::filesystem::absolute(path), ec);
            if (ec) {
                resolved = (path.is_absolute() ? path : std::filesystem::absolute(path)).lexically_normal();
            }
            return resolved;
        }

        std::filesystem::path lock_file_path(const std::filesystem::path &resolved) {
            return resolved.parent_path() / (resolved.filename().string() + ".lock");
        }

        // Cross-process, cross-instance mutual exclusion for the read-modify-
        // write cycle in load_or_create()/persist(). On POSIX this is scoped to
        // the open file description, so distinct AngleMappingManager instances
        // in the same process (different identities sharing one file) contend
        // on it exactly like separate processes would.
        class FileLockGuard {
        public:
            explicit FileLockGuard(const std::filesystem::path &lock_path) {
#if defined(_WIN32)
                handle_ =
                    ::CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle_ == INVALID_HANDLE_VALUE) {
                    throw TransportError("Failed to open angle mapping lock file: " + lock_path.string());
                }
                OVERLAPPED overlapped{};
                if (::LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &overlapped) == 0) {
                    ::CloseHandle(handle_);
                    handle_ = INVALID_HANDLE_VALUE;
                    throw TransportError("Failed to lock angle mapping file: " + lock_path.string());
                }
#else
                fd_ = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0644); // NOLINT
                if (fd_ < 0) {
                    throw TransportError("Failed to open angle mapping lock file: " + lock_path.string() + ": " +
                                         std::strerror(errno));
                }
                for (;;) {
                    if (::flock(fd_, LOCK_EX) == 0) {
                        break;
                    }
                    if (errno != EINTR) {
                        const int err = errno;
                        ::close(fd_);
                        fd_ = -1;
                        throw TransportError("Failed to lock angle mapping file: " + lock_path.string() + ": " +
                                             std::strerror(err));
                    }
                }
#endif
            }

            ~FileLockGuard() {
#if defined(_WIN32)
                if (handle_ != INVALID_HANDLE_VALUE) {
                    OVERLAPPED overlapped{};
                    ::UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped);
                    ::CloseHandle(handle_);
                }
#else
                if (fd_ >= 0) {
                    // Best-effort unlock: retry on EINTR a bounded number of
                    // times, then fall through to close(), which releases the
                    // flock() regardless as a safety net.
                    for (int attempt = 0; attempt < 8; ++attempt) {
                        if (::flock(fd_, LOCK_UN) == 0 || errno != EINTR) {
                            break;
                        }
                    }
                    ::close(fd_);
                }
#endif
            }

            FileLockGuard(const FileLockGuard &) = delete;
            FileLockGuard &operator=(const FileLockGuard &) = delete;
            FileLockGuard(FileLockGuard &&) = delete;
            FileLockGuard &operator=(FileLockGuard &&) = delete;

        private:
#if defined(_WIN32)
            HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
            int fd_ = -1;
#endif
        };

        // Reserves a filename that cannot collide with any other concurrent
        // writer (this process or another) targeting the same destination.
        std::filesystem::path make_unique_temp_path(const std::filesystem::path &target) {
            auto dir = target.parent_path();
            if (dir.empty()) {
                dir = ".";
            }
#if defined(_WIN32)
            std::mt19937_64 rng(std::random_device{}());
            for (int attempt = 0; attempt < 64; ++attempt) {
                const auto candidate = dir / (target.filename().string() + "." + std::to_string(rng()) + ".tmp");
                const HANDLE handle = ::CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                                    FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle != INVALID_HANDLE_VALUE) {
                    ::CloseHandle(handle);
                    return candidate;
                }
            }
            throw TransportError("Failed to create angle mapping temp file near: " + target.string());
#else
            auto templ = (dir / (target.filename().string() + ".XXXXXX")).string();
            std::vector<char> buffer(templ.begin(), templ.end());
            buffer.push_back('\0');
            const int fd = ::mkstemp(buffer.data()); // NOLINT
            if (fd < 0) {
                throw TransportError("Failed to create angle mapping temp file near: " + target.string() + ": " +
                                     std::strerror(errno));
            }
            ::close(fd);
            return std::filesystem::path(buffer.data());
#endif
        }

        void atomic_write(const std::filesystem::path &path, const std::string &text) {
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            const auto tmp = make_unique_temp_path(path);
            bool committed = false;
            struct TempCleanup {
                const std::filesystem::path &tmp;
                const bool &committed;
                ~TempCleanup() {
                    if (!committed) {
                        std::error_code ec;
                        std::filesystem::remove(tmp, ec);
                    }
                }
            } cleanup{tmp, committed};
            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out) {
                    throw TransportError("Failed to open angle mapping temp file: " + tmp.string());
                }
                out << text;
                out.flush();
                if (!out) {
                    throw TransportError("Failed to write angle mapping TOML: " + tmp.string());
                }
                out.close();
                if (!out) {
                    throw TransportError("Failed to close angle mapping TOML: " + tmp.string());
                }
            }
            // Only replace the destination once the temp file is known-good;
            // a write failure above must leave the original file untouched.
#if defined(_WIN32)
            if (!::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                ErrorContext context;
                context.operation = "angle_mapping.replace";
                context.native_status = static_cast<int>(::GetLastError());
                throw TransportError("Failed to replace angle mapping file", std::move(context));
            }
#else
            std::filesystem::rename(tmp, path);
#endif
            // Value is observed by RAII cleanup or subsequent Catch2 assertions.
            // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
            committed = true;
        }
    } // namespace

    AngleMapping default_angle_mapping(std::size_t joint_count) {
        AngleMapping mapping(joint_count);
        for (auto &row : mapping) {
            for (std::size_t i = 0; i < kAngleMappingColumns; ++i) {
                row[i] = static_cast<std::uint8_t>(i);
            }
        }
        return mapping;
    }

    std::filesystem::path default_angle_mapping_path() {
        std::filesystem::path base;
        if (const auto xdg = environment_path("XDG_CONFIG_HOME")) {
            base = expand_user(*xdg);
        } else {
#if defined(_WIN32)
            if (const auto local = environment_path("LOCALAPPDATA")) {
                base = *local;
            } else if (const auto roaming = environment_path("APPDATA")) {
                base = *roaming;
            } else
#endif
            {
                base = home_directory() / ".config";
            }
        }
        return base / "linkerbot" / "hand_angle_mappings.toml";
    }

    namespace {
        void validate_raw_values(const std::vector<std::uint8_t> &raw_values, std::size_t joint_count) {
            if (raw_values.size() != joint_count) {
                throw ValidationError("Expected " + std::to_string(joint_count) + " raw angle values, got " +
                                      std::to_string(raw_values.size()));
            }
        }

        AngleMapping validate_mapping(const AngleMapping &mapping, std::size_t joint_count) {
            if (mapping.size() != joint_count) {
                throw ValidationError("angle mapping must contain " + std::to_string(joint_count) + " rows, got " +
                                      std::to_string(mapping.size()));
            }
            for (std::size_t row = 0; row < mapping.size(); ++row) {
                if (mapping[row].size() != kAngleMappingColumns) {
                    throw ValidationError("angle mapping row " + std::to_string(row) + " must contain 256 values");
                }
            }
            return mapping;
        }

        AngleMapping mapping_from_ints(const std::vector<std::vector<int>> &rows, std::size_t joint_count) {
            if (rows.size() != joint_count) {
                throw ValidationError("angle mapping must contain " + std::to_string(joint_count) + " rows, got " +
                                      std::to_string(rows.size()));
            }
            AngleMapping mapping(joint_count);
            for (std::size_t row = 0; row < rows.size(); ++row) {
                if (rows[row].size() != kAngleMappingColumns) {
                    throw ValidationError("angle mapping row " + std::to_string(row) +
                                          " must contain 256 values, got " + std::to_string(rows[row].size()));
                }
                for (std::size_t col = 0; col < kAngleMappingColumns; ++col) {
                    const int value = rows[row][col];
                    if (value < 0 || value > 255) {
                        throw ValidationError("angle mapping row " + std::to_string(row) + " value " +
                                              std::to_string(col) + " out of range [0, 255]");
                    }
                    mapping[row][col] = static_cast<std::uint8_t>(value);
                }
            }
            return mapping;
        }

        std::string format_section(const std::string &model, const std::string &side, const std::string &interface_name,
                                   const std::vector<std::string> &joint_names, const AngleMapping &mapping) {
            std::ostringstream out;
            out << "model = \"" << escape_toml_string(model) << "\"\n";
            out << "side = \"" << escape_toml_string(side) << "\"\n";
            out << "interface_name = \"" << escape_toml_string(interface_name) << "\"\n";
            out << "joint_names = [";
            for (std::size_t i = 0; i < joint_names.size(); ++i) {
                if (i != 0) {
                    out << ", ";
                }
                out << '"' << escape_toml_string(joint_names[i]) << '"';
            }
            out << "]\n";
            out << "mapping = [\n";
            for (const auto &row : mapping) {
                out << "    [";
                for (std::size_t i = 0; i < row.size(); ++i) {
                    if (i != 0) {
                        out << ", ";
                    }
                    out << static_cast<int>(row[i]);
                }
                out << "],\n";
            }
            out << "]\n";
            return out.str();
        }

        std::optional<std::vector<std::string>> parse_optional_strings(std::string_view body, std::string_view key) {
            struct OptionalStringField {
                std::string_view body;
                std::string_view key;
            };
            const auto field = OptionalStringField{body, key};
            Cursor cursor(field.body);
            while (!cursor.done()) {
                const auto ident = cursor.read_ident();
                if (ident.empty()) {
                    return std::nullopt;
                }
                cursor.skip_ws_and_comments();
                if (!cursor.eat('=')) {
                    throw ValidationError("expected '=' after TOML key");
                }
                cursor.skip_ws_and_comments();
                if (ident == field.key) {
                    return cursor.parse_string_array();
                }
                cursor.skip_value();
            }
            return std::nullopt;
        }

        AngleMapping parse_mapping_field(std::string_view body, std::size_t joint_count) {
            Cursor cursor(body);
            while (!cursor.done()) {
                const auto ident = cursor.read_ident();
                if (ident.empty()) {
                    break;
                }
                cursor.skip_ws_and_comments();
                if (!cursor.eat('=')) {
                    throw ValidationError("expected '=' after TOML key");
                }
                cursor.skip_ws_and_comments();
                if (ident == "mapping") {
                    return mapping_from_ints(cursor.parse_int_matrix(), joint_count);
                }
                cursor.skip_value();
            }
            throw ValidationError("angle mapping section is missing mapping");
        }

        AngleMapping parse_section(const TomlTable &table, const std::string &model,
                                   const std::vector<std::string> &expected, std::size_t joint_count) {
            const auto names = parse_optional_strings(table.body, "joint_names");
            // Only accept the complete old ordered schema, never mixed aliases
            // or reordered names: calibration rows must keep their wire index.
            auto legacy = expected;
            if (model == "l20lite" && legacy.size() == 10) {
                if (legacy[1] == "thumb_roll") {
                    legacy[1] = "thumb_abd";
                }
                if (legacy[9] == "thumb_rotation") {
                    legacy[9] = "thumb_yaw";
                }
            } else if (model == "l20" && legacy.size() == 16) {
                if (legacy[0] == "thumb_rotation") {
                    legacy[0] = "thumb_abd";
                }
                if (legacy[1] == "thumb_roll") {
                    legacy[1] = "thumb_yaw";
                }
                for (auto &name : legacy) {
                    if (name.size() >= 5 && name.compare(name.size() - 5, 5, "_root") == 0) {
                        name += '1';
                    }
                }
            }
            if (names && *names != expected && *names != legacy) {
                throw ValidationError("Angle mapping section '" + table.key +
                                      "' joint_names do not match the current hand model");
            }
            return parse_mapping_field(table.body, joint_count);
        }

        // Replaces (or inserts) the `key` section of the TOML file at `path`
        // with `body`. Callers must already hold the cross-process file lock
        // for `path`'s normalized form so the read-modify-write is atomic with
        // respect to every other AngleMappingManager instance or process
        // touching the same file.
        void write_section_locked(const std::filesystem::path &path, const std::string &key, const std::string &body) {
            auto doc = parse_document(load_file(path));
            if (doc.preamble.find("version") == std::string::npos) {
                doc.preamble = "version = 1\n\n" + doc.preamble;
            }
            const auto header = "[\"" + escape_toml_string(key) + "\"]";
            bool replaced = false;
            for (auto &table : doc.tables) {
                if (table.key == key) {
                    table.header = header;
                    table.body = body;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                if (!doc.preamble.empty() && doc.preamble.back() != '\n') {
                    doc.preamble.push_back('\n');
                }
                doc.tables.push_back(TomlTable{key, header, body});
            }
            atomic_write(path, render_document(doc));
        }
    } // namespace

    AngleMappingManager::AngleMappingManager(std::string model, std::vector<std::string> joint_names,
                                             const std::optional<std::filesystem::path> &mapping_path, std::string side,
                                             std::string interface_name)
        : model_(std::move(model)), side_(side.empty() ? "unknown" : std::move(side)),
          interface_name_(interface_name.empty() ? "unknown" : std::move(interface_name)),
          joint_names_(std::move(joint_names)),
          path_(resolve_path(expand_user(mapping_path ? *mapping_path : default_angle_mapping_path()))),
          mapping_(load_or_create()) {}

    const std::filesystem::path &AngleMappingManager::path() const { return path_; }

    std::string AngleMappingManager::section_key() const { return model_ + ":" + side_ + ":" + interface_name_; }

    std::vector<std::uint8_t> AngleMappingManager::map_values(const std::vector<std::uint8_t> &raw_values) const {
        validate_raw_values(raw_values, joint_names_.size());
        const std::shared_lock lock(mapping_mutex_);
        std::vector<std::uint8_t> mapped;
        mapped.reserve(raw_values.size());
        for (std::size_t i = 0; i < raw_values.size(); ++i) {
            mapped.push_back(mapping_[i][raw_values[i]]);
        }
        return mapped;
    }

    AngleMapping AngleMappingManager::get_mapping() const {
        const std::shared_lock lock(mapping_mutex_);
        return mapping_;
    }

    AngleMapping AngleMappingManager::get_default_mapping() const { return default_angle_mapping(joint_names_.size()); }

    void AngleMappingManager::set_mapping(const AngleMapping &mapping) {
        auto validated = validate_mapping(mapping, joint_names_.size());
        const std::unique_lock lock(mapping_mutex_);
        persist(validated);
        mapping_ = std::move(validated);
    }

    void AngleMappingManager::reset_mapping() { set_mapping(default_angle_mapping(joint_names_.size())); }

    // load_or_create() runs during construction, before any other member
    // function can observe `this`, so it does not need mapping_mutex_; it
    // still needs the cross-process file lock because another
    // AngleMappingManager (this process or another) may be reading/writing
    // the same underlying file concurrently.
    AngleMapping AngleMappingManager::load_or_create() {
        if (path_.has_parent_path()) {
            std::filesystem::create_directories(path_.parent_path());
        }
        const FileLockGuard file_lock(lock_file_path(path_));

        auto doc = parse_document(load_file(path_));
        const auto key = section_key();
        for (const auto &table : doc.tables) {
            if (table.key == key) {
                return parse_section(table, model_, joint_names_, joint_names_.size());
            }
        }
        const auto mapping = default_angle_mapping(joint_names_.size());
        const auto body = format_section(model_, side_, interface_name_, joint_names_, mapping);
        write_section_locked(path_, key, body);
        return mapping;
    }

    // Precondition: caller holds mapping_mutex_ (see set_mapping()). Acquires
    // the cross-process file lock for the duration of the read-modify-write so
    // two identities sharing one file — whether different AngleMappingManager
    // instances in this process or a completely separate process — cannot
    // interleave reads/writes and drop each other's sections.
    void AngleMappingManager::persist(const AngleMapping &mapping) {
        if (path_.has_parent_path()) {
            std::filesystem::create_directories(path_.parent_path());
        }
        const FileLockGuard file_lock(lock_file_path(path_));

        const auto key = section_key();
        const auto body = format_section(model_, side_, interface_name_, joint_names_, mapping);
        write_section_locked(path_, key, body);
    }
} // namespace linkerbot::hand

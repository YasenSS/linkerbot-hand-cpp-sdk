#include <linkerbot/hand/identity.hpp>

#include <algorithm>

namespace linkerbot::hand {
    namespace {
        std::string trim(std::string_view value) {
            const auto zero = value.find('\0');
            value = value.substr(0, zero);
            for (const auto c : value) {
                if (static_cast<unsigned char>(c) > 0x7F) {
                    throw IdentityProtocolError("identity is not ASCII");
                }
            }
            const auto first = value.find_first_not_of(" \t\r\n\v\f");
            if (first == std::string_view::npos) {
                return {};
            }
            return std::string(value.substr(first, value.find_last_not_of(" \t\r\n\v\f") - first + 1));
        }
    }

    namespace identity_detail {
        std::string upper(std::string value) {
            for (auto &c : value) {
                if (c >= 'a' && c <= 'z') {
                    c = static_cast<char>(c - 'a' + 'A');
                }
            }
            return value;
        }

        std::vector<std::string> split_product(std::string_view value) {
            const auto text = trim(value);
            std::vector<std::string> parts;
            size_t start = 0;
            for (;;) {
                const auto end = text.find('-', start);
                parts.push_back(trim(std::string_view(text).substr(start, end - start)));
                if (end == std::string::npos) {
                    return parts;
                }
                start = end + 1;
            }
        }

        std::optional<IdentitySide> parse_side(std::string_view value) {
            const auto side = upper(trim(value));
            if (side.empty()) {
                return {};
            }
            if (side == "L" || side == "LEFT") {
                return IdentitySide::kLeft;
            }
            if (side == "R" || side == "RIGHT") {
                return IdentitySide::kRight;
            }
            throw IdentityProtocolError("identity has unknown side: " + side);
        }
    }

    namespace {
        template <typename ExpectedModels>
        FactoryIdentity parse_factory_identity_impl(std::string_view product_code,
                                                    const ExpectedModels &expected_models, IdentitySide expected_side) {
            const auto parts = identity_detail::split_product(product_code);
            if (parts.size() < 4 ||
                std::any_of(parts.begin(), parts.begin() + 4, [](const auto &s) { return s.empty(); })) {
                throw IdentityProtocolError("factory product code is incomplete");
            }
            const auto model = identity_detail::upper(parts[0]);
            if (std::none_of(expected_models.begin(), expected_models.end(),
                             [&](auto expected) { return model == identity_detail::upper(std::string(expected)); })) {
                throw IdentityMismatchError("unexpected factory model: " + parts[0]);
            }
            const auto label = identity_detail::upper(parts[3]);
            if (label != "L" && label != "R") {
                throw IdentityProtocolError("factory product code has unknown side");
            }
            const auto side = label == "L" ? IdentitySide::kLeft : IdentitySide::kRight;
            if (side != expected_side) {
                throw IdentityMismatchError("factory side does not match configured endpoint");
            }
            return {trim(product_code), parts[0], parts[2], side};
        }
    }

    FactoryIdentity parse_factory_identity(std::string_view product_code,
                                           std::initializer_list<std::string_view> expected_models,
                                           IdentitySide expected_side) {
        return parse_factory_identity_impl(product_code, expected_models, expected_side);
    }

    FactoryIdentity parse_factory_identity(std::string_view product_code,
                                           const std::vector<std::string_view> &expected_models,
                                           IdentitySide expected_side) {
        return parse_factory_identity_impl(product_code, expected_models, expected_side);
    }
}

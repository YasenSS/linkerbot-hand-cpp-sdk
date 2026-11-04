#pragma once

#include <linkerbot/hand/errors.hpp>
#include <optional>

namespace linkerbot::hand::detail {
    // Internal invariants must fail explicitly instead of dereferencing an empty
    // optional. The caller retains its existing synchronization and ownership.
    template <class T> T &checked_value(std::optional<T> &value, const char *message) {
        if (!value.has_value()) {
            throw InternalError(message);
        }
        return *value;
    }
    template <class T> const T &checked_value(const std::optional<T> &value, const char *message) {
        if (!value.has_value()) {
            throw InternalError(message);
        }
        return *value;
    }
    template <class T> const T &checked_value(const std::optional<T> &&, const char *) = delete;
}

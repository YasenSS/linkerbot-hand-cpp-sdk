#pragma once

#include <optional>
#include <stdexcept>
#include <utility>

namespace test_support {
    // Own the result so temporaries and nested optionals cannot leave dangling
    // references. Missing data is a failed test, never an unchecked access.
    template <class T> T require_value(std::optional<T> value) {
        if (!value.has_value()) {
            throw std::logic_error("Expected an engaged optional in test");
        }
        return std::move(*value);
    }
}

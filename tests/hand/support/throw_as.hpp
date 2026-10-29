#pragma once

namespace test_util {

    // Give callbacks that always throw a concrete deduced return type.
    template <typename Result, typename Error> [[noreturn]] Result throw_as(Error error) { throw error; }

} // namespace test_util

#pragma once

namespace linkerbot {

    template <class... Ts> struct Overloaded : Ts... {
        using Ts::operator()...;
    };

    template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace linkerbot

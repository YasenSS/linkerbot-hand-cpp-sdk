format:
    fd -e hpp -e cpp -X clang-format -i

format-check:
    git ls-files '*.cpp' '*.hpp' '*.h' | xargs -r clang-format --dry-run --Werror

configure *args:
    cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON {{args}}
    ln -sfn build/compile_commands.json compile_commands.json

build *args:
    cmake --build build {{args}}

example:
    cmake --build build --target example

test:
    cmake --build build --target tests
    ctest --test-dir build --output-on-failure

test-hardware:
    cmake --build build --target tests
    ./build/tests/tests "[l6]"

test-hardware-o6:
    cmake --build build --target tests
    ./build/tests/tests "[o6]"

test-hardware-l20:
    cmake --build build --target tests
    ./build/tests/tests "[l20]"

test-hardware-l20lite:
    cmake --build build --target tests
    ./build/tests/tests "[l20lite]"

package:
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DLINKERBOT_BUILD_TESTS=OFF
    cmake --build build -j"$(nproc)" --target linkerbot-cpp-sdk example
    cmake --install build --prefix dist
    cmake --build build --target package

lint: configure
    run-clang-tidy

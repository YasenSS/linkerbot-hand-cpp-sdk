if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "SocketCAN read/version and benchmark tools are Linux-only; use windows_readonly on Windows")
    return()
endif()

add_executable(l6_frequency_benchmark EXCLUDE_FROM_ALL
    l6_frequency_benchmark.cpp
    l6_raw_session.cpp
)
target_link_libraries(l6_frequency_benchmark PRIVATE linkerbot::cpp-sdk)
set(LINKERBOT_BENCHMARK_SOURCE_REVISION "" CACHE STRING "Source revision for an archived benchmark checkout")
set(_benchmark_revision "${LINKERBOT_BENCHMARK_SOURCE_REVISION}")
set(_benchmark_dirty "unknown")
if(EXISTS "${PROJECT_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(GIT_FOUND)
        if(NOT _benchmark_revision)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse HEAD
                OUTPUT_VARIABLE _benchmark_revision OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" status --porcelain
            OUTPUT_VARIABLE _benchmark_status OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(_benchmark_dirty "clean")
        if(_benchmark_status)
            set(_benchmark_dirty "modified")
        endif()
    endif()
endif()
if(NOT _benchmark_revision)
    set(_benchmark_revision "unknown")
endif()
target_compile_definitions(l6_frequency_benchmark PRIVATE
    L6_BENCHMARK_BUILD_TYPE="$<CONFIG>"
    L6_BENCHMARK_SDK_COMMIT="${_benchmark_revision}"
    L6_BENCHMARK_SDK_STATE="${_benchmark_dirty}"
)

add_executable(o6_frequency_benchmark EXCLUDE_FROM_ALL
    o6_frequency_benchmark.cpp
)
target_link_libraries(o6_frequency_benchmark PRIVATE linkerbot::cpp-sdk)
set_target_properties(o6_frequency_benchmark PROPERTIES
    OUTPUT_NAME o6_frequency_benchmark)
target_compile_definitions(o6_frequency_benchmark PRIVATE
    O6_BENCHMARK_BUILD_TYPE="$<CONFIG>"
)

add_executable(l6_read_version EXCLUDE_FROM_ALL l6_read_version.cpp)
target_link_libraries(l6_read_version PRIVATE linkerbot::cpp-sdk)

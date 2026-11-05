# AUTO preserves the legacy source-directory behavior. PREBUILT never adds a
# source checkout, even when a sibling checkout or an old source cache exists.
set(LINKERBOT_COMM_PROVIDER AUTO CACHE STRING "Transport dependency: AUTO, SOURCE or PREBUILT")
set_property(CACHE LINKERBOT_COMM_PROVIDER PROPERTY STRINGS AUTO SOURCE PREBUILT)
string(TOUPPER "${LINKERBOT_COMM_PROVIDER}" _comm_provider)
if(NOT _comm_provider MATCHES "^(AUTO|SOURCE|PREBUILT)$")
    message(FATAL_ERROR "Invalid LINKERBOT_COMM_PROVIDER '${LINKERBOT_COMM_PROVIDER}'; use AUTO, SOURCE or PREBUILT")
endif()
set(LINKERBOT_COMM_PREBUILT_PREFIX "" CACHE PATH "Extracted/installed transport SDK root (contains include and lib)")

# Type CLI paths even when supplied before this include. Untyped Windows
# values otherwise retain backslashes that CMake can interpret as escapes when
# source paths are expanded by downstream test/fixture commands.
set(_comm_default_source "")
if(NOT DEFINED LINKERBOT_COMM_SOURCE_DIR AND
        EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../linkerbot-transport-cpp/CMakeLists.txt")
    set(_comm_default_source "${CMAKE_CURRENT_SOURCE_DIR}/../linkerbot-transport-cpp")
endif()
set(LINKERBOT_COMM_SOURCE_DIR "${_comm_default_source}" CACHE PATH "Transport source checkout")
if(LINKERBOT_COMM_SOURCE_DIR)
    file(TO_CMAKE_PATH "${LINKERBOT_COMM_SOURCE_DIR}" LINKERBOT_COMM_SOURCE_DIR)
    get_filename_component(LINKERBOT_COMM_SOURCE_DIR "${LINKERBOT_COMM_SOURCE_DIR}"
        ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(TARGET linkerbot::comm)
    # Embedding projects retain ownership of a dependency supplied in advance.
    message(STATUS "Transport: using parent-provided linkerbot::comm")
    return()
endif()

if(_comm_provider STREQUAL "AUTO")
    if(LINKERBOT_COMM_PREBUILT_PREFIX)
        set(_comm_provider PREBUILT)
    elseif(LINKERBOT_COMM_SOURCE_DIR)
        set(_comm_provider SOURCE)
    else()
        set(_comm_provider PREBUILT)
    endif()
endif()

if(_comm_provider STREQUAL "SOURCE")
    if(NOT LINKERBOT_COMM_SOURCE_DIR OR NOT EXISTS "${LINKERBOT_COMM_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "SOURCE requires LINKERBOT_COMM_SOURCE_DIR pointing to a transport checkout")
    endif()
    message(STATUS "Transport: SOURCE (${LINKERBOT_COMM_SOURCE_DIR})")
    add_subdirectory("${LINKERBOT_COMM_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/linkerbot-transport-cpp")
else()
    if(LINKERBOT_COMM_PREBUILT_PREFIX)
        # Restrict discovery to the requested SDK, and reject cached package
        # directories outside it rather than silently linking a different SDK.
        # v0.3.3's comm package forwards to linkerbot-transport. Make the
        # requested prefix visible to that nested find_dependency as well,
        # without changing the caller's package search path.
        function(_linkerbot_find_prebuilt_comm)
            list(PREPEND CMAKE_PREFIX_PATH "${LINKERBOT_COMM_PREBUILT_PREFIX}")
            find_package(linkerbot-comm ${PROJECT_VERSION} CONFIG REQUIRED
                PATHS "${LINKERBOT_COMM_PREBUILT_PREFIX}" NO_DEFAULT_PATH
                NO_CMAKE_FIND_ROOT_PATH)
        endfunction()
        _linkerbot_find_prebuilt_comm()
        get_filename_component(_comm_requested_prefix "${LINKERBOT_COMM_PREBUILT_PREFIX}" REALPATH)
        foreach(_comm_package linkerbot-comm linkerbot-transport)
            if(DEFINED ${_comm_package}_DIR AND NOT "${${_comm_package}_DIR}" MATCHES "-NOTFOUND$")
                get_filename_component(_comm_found_dir "${${_comm_package}_DIR}" REALPATH)
                file(RELATIVE_PATH _comm_relative "${_comm_requested_prefix}" "${_comm_found_dir}")
                if(IS_ABSOLUTE "${_comm_relative}" OR _comm_relative MATCHES "^\\.\\.(/|$)")
                    message(FATAL_ERROR "Cached ${_comm_package}_DIR is outside LINKERBOT_COMM_PREBUILT_PREFIX; use a fresh build directory or clear ${_comm_package}_DIR")
                endif()
            endif()
        endforeach()
    else()
        find_package(linkerbot-comm ${PROJECT_VERSION} CONFIG REQUIRED)
    endif()
    message(STATUS "Transport: PREBUILT (${linkerbot-comm_DIR})")
endif()

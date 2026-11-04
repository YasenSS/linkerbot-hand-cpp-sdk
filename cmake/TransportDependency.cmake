# AUTO preserves the legacy source-directory behavior. PREBUILT never adds a
# source checkout, even when a sibling checkout or an old source cache exists.
set(LINKERBOT_COMM_PROVIDER AUTO CACHE STRING "Transport dependency: AUTO, SOURCE or PREBUILT")
set_property(CACHE LINKERBOT_COMM_PROVIDER PROPERTY STRINGS AUTO SOURCE PREBUILT)
string(TOUPPER "${LINKERBOT_COMM_PROVIDER}" _comm_provider)
if(NOT _comm_provider MATCHES "^(AUTO|SOURCE|PREBUILT)$")
    message(FATAL_ERROR "Invalid LINKERBOT_COMM_PROVIDER '${LINKERBOT_COMM_PROVIDER}'; use AUTO, SOURCE or PREBUILT")
endif()
set(LINKERBOT_COMM_PREBUILT_PREFIX "" CACHE PATH "Extracted/installed transport SDK root (contains include and lib)")

# Respect an explicitly empty legacy path as a request to find a package.
if(NOT DEFINED LINKERBOT_COMM_SOURCE_DIR)
    set(_comm_default_source "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../linkerbot-transport-cpp/CMakeLists.txt")
        set(_comm_default_source "${CMAKE_CURRENT_SOURCE_DIR}/../linkerbot-transport-cpp")
    endif()
    set(LINKERBOT_COMM_SOURCE_DIR "${_comm_default_source}" CACHE PATH "Transport source checkout")
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
        find_package(linkerbot-comm ${PROJECT_VERSION} CONFIG REQUIRED
            PATHS "${LINKERBOT_COMM_PREBUILT_PREFIX}" NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH)
        get_filename_component(_comm_requested_prefix "${LINKERBOT_COMM_PREBUILT_PREFIX}" REALPATH)
        get_filename_component(_comm_found_dir "${linkerbot-comm_DIR}" REALPATH)
        file(RELATIVE_PATH _comm_relative "${_comm_requested_prefix}" "${_comm_found_dir}")
        if(IS_ABSOLUTE "${_comm_relative}" OR _comm_relative MATCHES "^\\.\\.(/|$)")
            message(FATAL_ERROR "Cached linkerbot-comm_DIR is outside LINKERBOT_COMM_PREBUILT_PREFIX; use a fresh build directory or clear linkerbot-comm_DIR")
        endif()
    else()
        find_package(linkerbot-comm ${PROJECT_VERSION} CONFIG REQUIRED)
    endif()
    message(STATUS "Transport: PREBUILT (${linkerbot-comm_DIR})")
endif()

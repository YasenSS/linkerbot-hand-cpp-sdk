cmake_minimum_required(VERSION 3.16)

project(linkerbot-hand-cpp
    VERSION 0.3.0
    DESCRIPTION "LinkerBot dexterous hand C++ SDK"
    LANGUAGES CXX
)
if(MSVC)
    add_compile_options(/utf-8)
    if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(LINKERBOT_IS_TOP_LEVEL ON)
else()
    set(LINKERBOT_IS_TOP_LEVEL OFF)
endif()

option(LINKERBOT_BUILD_EXAMPLES "Build example programs" ${LINKERBOT_IS_TOP_LEVEL})
option(LINKERBOT_BUILD_TESTS "Build tests" ${LINKERBOT_IS_TOP_LEVEL})
option(LINKERBOT_BUILD_TOOLS "Build developer tools" ${LINKERBOT_IS_TOP_LEVEL})

if(LINKERBOT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(LINKERBOT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# An explicit source path binds paired development checkouts. An empty path
# consumes an installed package and does not require a sibling source tree.
set(_linkerbot_default_comm "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../linkerbot-transport-cpp/CMakeLists.txt")
    set(_linkerbot_default_comm "${CMAKE_CURRENT_SOURCE_DIR}/../linkerbot-transport-cpp")
endif()
set(LINKERBOT_COMM_SOURCE_DIR "${_linkerbot_default_comm}"
    CACHE PATH "Paired comm source tree; empty selects find_package")
option(LINKERBOT_ALLOW_DEPENDENCY_DOWNLOADS "Allow missing test dependencies to be fetched" ON)
if(NOT TARGET linkerbot::comm)
    if(LINKERBOT_COMM_SOURCE_DIR)
        if(NOT EXISTS "${LINKERBOT_COMM_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "Invalid LINKERBOT_COMM_SOURCE_DIR: ${LINKERBOT_COMM_SOURCE_DIR}")
        endif()
        add_subdirectory("${LINKERBOT_COMM_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/linkerbot-transport-cpp")
    else()
        find_package(linkerbot-comm ${PROJECT_VERSION} CONFIG REQUIRED)
    endif()
endif()

# Check both paired sources and a target supplied by the parent project.
get_target_property(_linkerbot_comm_version linkerbot::comm LINKERBOT_COMM_VERSION)
string(REGEX MATCH "^[0-9]+\\.[0-9]+" _linkerbot_comm_line "${_linkerbot_comm_version}")
if(NOT _linkerbot_comm_line STREQUAL "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
   OR _linkerbot_comm_version VERSION_LESS PROJECT_VERSION)
    message(FATAL_ERROR
        "linkerbot-hand-cpp ${PROJECT_VERSION} requires linkerbot-comm "
        "${PROJECT_VERSION} or newer in the same minor line; found '${_linkerbot_comm_version}'. "
        "Update the paired comm checkout or installed package.")
endif()

file(GLOB_RECURSE LINKERBOT_SDK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(linkerbot-cpp-sdk STATIC ${LINKERBOT_SDK_SOURCES})
add_library(linkerbot::cpp-sdk ALIAS linkerbot-cpp-sdk)

target_include_directories(linkerbot-cpp-sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(linkerbot-cpp-sdk PUBLIC cxx_std_17)
# comm-compat keeps flat includes (`"can_dispatcher.hpp"`) working for
# in-flight consumers; canonical includes are <linkerbot/can/...>.
target_link_libraries(linkerbot-cpp-sdk
    PUBLIC
        Threads::Threads
        linkerbot::comm-compat
)

if(LINKERBOT_BUILD_TESTS)
    enable_testing()
endif()

if(LINKERBOT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(LINKERBOT_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(LINKERBOT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

set_target_properties(linkerbot-cpp-sdk PROPERTIES EXPORT_NAME cpp-sdk)
install(TARGETS linkerbot-cpp-sdk EXPORT linkerbot-hand-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT linkerbot-hand-targets NAMESPACE linkerbot::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/linkerbot-hand-cpp)
configure_package_config_file(cmake/linkerbot-hand-cpp-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/linkerbot-hand-cpp-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/linkerbot-hand-cpp)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/linkerbot-hand-cpp-config-version.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/linkerbot-hand-cpp-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/linkerbot-hand-cpp-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/linkerbot-hand-cpp)
install(FILES LICENSE README.md README.en.md NOTICE CHANGELOG.md CONTRIBUTING.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/linkerbot-hand-cpp)
install(FILES docs/README.md docs/COMPATIBILITY.md docs/windows.md docs/usage.md
    docs/model-capabilities.md docs/windows-adapters.md docs/hand-device-discovery.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/linkerbot-hand-cpp/docs)
install(FILES validation/installed-consumer/CMakeLists.txt validation/installed-consumer/main.cpp
    DESTINATION ${CMAKE_INSTALL_DATADIR}/linkerbot-hand-cpp/validation/installed-consumer)
install(FILES validation/fake_classic_hand.hpp
    DESTINATION ${CMAKE_INSTALL_DATADIR}/linkerbot-hand-cpp/validation)

include(cmake/ReleasePackage.cmake)

if(LINKERBOT_IS_TOP_LEVEL)
    set(CPACK_PACKAGE_NAME "linkerbot-hand-cpp")
    set(CPACK_PACKAGE_VENDOR "LinkerBot")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "LinkerBot dexterous hand C++ SDK")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_PACKAGE_CHECKSUM SHA256)
    if(WIN32)
        set(CPACK_GENERATOR ZIP)
        configure_file(cmake/WindowsCPack.cmake.in WindowsCPack.cmake @ONLY)
        set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/WindowsCPack.cmake")
    else()
        set(CPACK_GENERATOR "TGZ")
    endif()
    set(CPACK_PACKAGE_FILE_NAME
        "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}")
    include(CPack)
endif()

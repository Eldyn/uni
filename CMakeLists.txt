cmake_minimum_required(VERSION 3.15)
project(UNI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build with TLS (uWS::SSLApp) by default. Turn OFF for plain HTTP when a host
# terminates TLS at its edge and proxies HTTP to the container (e.g. Render):
#   cmake --preset conan-release -DUNI_ENABLE_SSL=OFF
option(UNI_ENABLE_SSL "Build the server with TLS (uWS::SSLApp)" ON)

# --- Contract code generation (hatchbed/asyncapi_gencpp) ---
# Fetches the generator at configure time, then runs it as a pre-build step.
# Source of truth: contract/asyncapi.yaml
# Output:          ${CMAKE_BINARY_DIR}/generated/ws/messages.h  (namespace ws::)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(FetchContent)
FetchContent_Declare(
    asyncapi_gencpp
    GIT_REPOSITORY https://github.com/hatchbed/asyncapi_gencpp.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
# Populate only (no add_subdirectory) — the package is ROS-specific and only
# the Python generator script is needed here. MakeAvailable would call
# add_subdirectory and pull in its ROS-specific CMake, so the single-argument
# FetchContent_Populate is intentional; CMP0169 OLD allows it without warnings.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(asyncapi_gencpp)
if(NOT asyncapi_gencpp_POPULATED)
    FetchContent_Populate(asyncapi_gencpp)
endif()

set(WS_CONTRACT_YAML   "${CMAKE_SOURCE_DIR}/contract/asyncapi.yaml")
set(WS_GEN_SCRIPT      "${asyncapi_gencpp_SOURCE_DIR}/src/asyncapi_gencpp.py")
# The script writes: {outdir}/{prefix}/messages.h  (one .h per schema + master messages.h)
set(WS_MESSAGES_H      "${CMAKE_BINARY_DIR}/generated/ws/messages.h")

add_custom_command(
    OUTPUT  "${WS_MESSAGES_H}"
    COMMAND "${Python3_EXECUTABLE}" "${WS_GEN_SCRIPT}"
            "${WS_CONTRACT_YAML}" "ws" "${CMAKE_BINARY_DIR}/generated"
    DEPENDS "${WS_CONTRACT_YAML}" "${WS_GEN_SCRIPT}"
    COMMENT "Generating WebSocket payload structs from asyncapi.yaml (hatchbed/asyncapi_gencpp)"
)
add_custom_target(gen_ws_messages DEPENDS "${WS_MESSAGES_H}")

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(libuv REQUIRED)
find_package(uwebsockets REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(jwt-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)

# --- Main Executable ---
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(uni_server ${SOURCES})

# --- Fix for Winsock conflicts on Windows ---
if(WIN32)
    target_compile_definitions(uni_server PRIVATE WIN32_LEAN_AND_MEAN)
endif()

target_compile_definitions(uni_server PRIVATE UNI_ENABLE_SSL=$<BOOL:${UNI_ENABLE_SSL}>)

target_include_directories(uni_server PRIVATE include src "${CMAKE_BINARY_DIR}/generated")
add_dependencies(uni_server gen_ws_messages)

# --- Linking ---
target_link_libraries(uni_server PRIVATE 
    OpenSSL::SSL 
    OpenSSL::Crypto
    ZLIB::ZLIB
    libuv::uv_a
    uwebsockets::uwebsockets
    SQLite::SQLite3
    jwt-cpp::jwt-cpp
    nlohmann_json::nlohmann_json
)

# --- Copy the runtime assets next to the executable ---
# The server resolves the certificates, .env file and public/ folder starting from
# the current working directory. By copying them next to the binary, the executable can
# be launched either from the project root or directly from its own build folder
# (e.g. build/Release/).
#
# Note: the presence of the files is evaluated at configuration time (CMake), so
# the certificates, .env and public/ must be generated BEFORE `cmake --preset`. The setup
# script creates them in the correct order.
set(RUNTIME_FILES cert.pem key.pem .env)
foreach(asset IN LISTS RUNTIME_FILES)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${asset}")
        add_custom_command(TARGET uni_server POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/${asset}"
                    "$<TARGET_FILE_DIR:uni_server>/${asset}"
            COMMENT "Copying ${asset} next to the executable")
    else()
        message(WARNING "Runtime asset '${asset}' not found: it will not be copied next to the executable.")
    endif()
endforeach()

if(EXISTS "${CMAKE_SOURCE_DIR}/public")
    add_custom_command(TARGET uni_server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/public"
                "$<TARGET_FILE_DIR:uni_server>/public"
        COMMENT "Copying the public/ folder next to the executable")
else()
    message(WARNING "Folder 'public/' not found: build the frontend first (cd frontend && npm run build).")
endif()

# --- Unit tests (doctest) ---
# On by default so the test TUs land in compile_commands.json and clangd/LSP can
# resolve <doctest/doctest.h>. Configure with -DUNI_BUILD_TESTS=OFF for a lighter
# build (skips the doctest dependency and the uni_tests target). Each tested
# module is compiled into uni_tests alongside the test sources (the main
# executable's TU carries main(), so it cannot be linked here).
option(UNI_BUILD_TESTS "Build the C++ unit tests (doctest)" ON)
if(UNI_BUILD_TESTS)
    find_package(doctest REQUIRED)
    enable_testing()

    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "tests/unit/*.cpp")
    # Modules under test are compiled into uni_tests directly (the main
    # executable's TU owns main(), so src cannot be linked wholesale).
    add_executable(uni_tests
        ${TEST_SOURCES}
        src/common/rate_limiter.cpp
        src/common/login_throttle.cpp
    )
    target_include_directories(uni_tests PRIVATE include src "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(uni_tests PRIVATE
        doctest::doctest
        nlohmann_json::nlohmann_json
    )
    add_dependencies(uni_tests gen_ws_messages)

    add_test(NAME unit COMMAND uni_tests)
endif()

message(STATUS "Configuration complete for system: ${CMAKE_SYSTEM_NAME}")
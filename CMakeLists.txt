cmake_minimum_required(VERSION 3.15)

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" UNI_VERSION)
# VERSION may carry a SemVer pre-release/build suffix (e.g. 0.2.0-rc.1); strip it
# for project(), which only accepts a numeric major[.minor[.patch[.tweak]]].
string(REGEX REPLACE "[-+].*$" "" UNI_VERSION_NUMERIC "${UNI_VERSION}")
project(UNI VERSION ${UNI_VERSION_NUMERIC} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build with TLS (uWS::SSLApp) by default. Turn OFF for plain HTTP when a host
# terminates TLS at its edge and proxies HTTP to the container (e.g. Render):
#   cmake --preset conan-release -DUNI_ENABLE_SSL=OFF
option(UNI_ENABLE_SSL "Build the server with TLS (uWS::SSLApp)" ON)

# Exclude dev-only content (e.g. mods/test) from the build. The Docker image
# configures this ON so test content never reaches production.
option(UNI_PROD_BUILD "Exclude dev-only content from the build" OFF)
if(UNI_PROD_BUILD)
    add_compile_definitions(UNI_PROD_BUILD)
endif()

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
# Populate only (no add_subdirectory), the package is ROS-specific and only
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
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "-- Generating WebSocket payload structs from asyncapi.yaml"
    COMMAND "${Python3_EXECUTABLE}" "${WS_GEN_SCRIPT}"
            "${WS_CONTRACT_YAML}" "ws" "${CMAKE_BINARY_DIR}/generated"
    DEPENDS "${WS_CONTRACT_YAML}" "${WS_GEN_SCRIPT}"
)
add_custom_target(gen_ws_messages DEPENDS "${WS_MESSAGES_H}")

# --- Validation constants (common/contract.hpp) ---
# Derives <common/contract.hpp> (namespace contract::) from the same AsyncAPI
# constraint schemas the frontend reads, so the backend's validation limits are
# never hand-maintained. Mirrors the ws-message generation above.
# Source of truth: contract/asyncapi.yaml
# Output:          ${CMAKE_BINARY_DIR}/generated/common/contract.hpp
set(CONTRACT_HPP_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/generate_contract_hpp.py")
set(CONTRACT_HPP_OUT    "${CMAKE_BINARY_DIR}/generated/common/contract.hpp")
add_custom_command(
    OUTPUT  "${CONTRACT_HPP_OUT}"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "-- Generating validation constants from asyncapi.yaml"
    COMMAND "${Python3_EXECUTABLE}" "${CONTRACT_HPP_SCRIPT}"
            "${WS_CONTRACT_YAML}" "${CONTRACT_HPP_OUT}"
    DEPENDS "${WS_CONTRACT_YAML}" "${CONTRACT_HPP_SCRIPT}"
)
add_custom_target(gen_contract_hpp DEPENDS "${CONTRACT_HPP_OUT}")

# --- WebSocket action constants (ws/actions.hpp) ---
# Derives <ws/actions.hpp> (namespace ws::) from the channel publish/subscribe
# lists in the AsyncAPI contract. Replaces the hand-maintained ClientAction
# struct and ServerAction enum that previously lived in include/common/ws.hpp.
# Source of truth: contract/asyncapi.yaml
# Output:          ${CMAKE_BINARY_DIR}/generated/ws/actions.hpp
set(WS_ACTIONS_HPP_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/generate_ws_hpp.py")
set(WS_ACTIONS_HPP_OUT    "${CMAKE_BINARY_DIR}/generated/ws/actions.hpp")
add_custom_command(
    OUTPUT  "${WS_ACTIONS_HPP_OUT}"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "-- Generating WebSocket action constants from asyncapi.yaml"
    COMMAND "${Python3_EXECUTABLE}" "${WS_ACTIONS_HPP_SCRIPT}"
            "${WS_CONTRACT_YAML}" "${WS_ACTIONS_HPP_OUT}"
    DEPENDS "${WS_CONTRACT_YAML}" "${WS_ACTIONS_HPP_SCRIPT}"
)
add_custom_target(gen_ws_actions_hpp DEPENDS "${WS_ACTIONS_HPP_OUT}")

# --- Localized email copy (common/email_copy.hpp) ---
# Derives <common/email_copy.hpp> (namespace email_copy::) from the
# 'email_*'-prefixed keys in the frontend's translated message bundles, so
# outbound transactional email never hand-duplicates UI copy.
# Source of truth: frontend/messages/<locale>.json
# Output:          ${CMAKE_BINARY_DIR}/generated/common/email_copy.hpp
set(EMAIL_COPY_HPP_SCRIPT  "${CMAKE_SOURCE_DIR}/scripts/generate_email_copy_hpp.py")
set(EMAIL_MESSAGES_DIR     "${CMAKE_SOURCE_DIR}/frontend/messages")
set(EMAIL_COPY_HPP_OUT     "${CMAKE_BINARY_DIR}/generated/common/email_copy.hpp")
file(GLOB EMAIL_MESSAGES_JSON CONFIGURE_DEPENDS "${EMAIL_MESSAGES_DIR}/*.json")
add_custom_command(
    OUTPUT  "${EMAIL_COPY_HPP_OUT}"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "-- Generating localized email copy from frontend/messages"
    COMMAND "${Python3_EXECUTABLE}" "${EMAIL_COPY_HPP_SCRIPT}"
            "${EMAIL_MESSAGES_DIR}" "${EMAIL_COPY_HPP_OUT}"
    DEPENDS "${EMAIL_MESSAGES_JSON}" "${EMAIL_COPY_HPP_SCRIPT}"
)
add_custom_target(gen_email_copy_hpp DEPENDS "${EMAIL_COPY_HPP_OUT}")

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(libuv REQUIRED)
find_package(uwebsockets REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(jwt-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CURL REQUIRED)

# --- Main Executable ---
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(uni_server ${SOURCES})

# --- Fix for Winsock conflicts on Windows ---
if(WIN32)
    target_compile_definitions(uni_server PRIVATE WIN32_LEAN_AND_MEAN)
endif()

target_compile_definitions(uni_server PRIVATE UNI_ENABLE_SSL=$<BOOL:${UNI_ENABLE_SSL}> UNI_HAS_LIBCURL=1)

target_include_directories(uni_server PRIVATE include src "${CMAKE_BINARY_DIR}/generated")
add_dependencies(uni_server gen_ws_messages gen_contract_hpp gen_ws_actions_hpp gen_email_copy_hpp)

# --- Linking ---
target_link_libraries(uni_server PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    libuv::uv_a
    uwebsockets::uwebsockets
    SQLite::SQLite3
    jwt-cpp::jwt-cpp
    nlohmann_json::nlohmann_json
    CURL::libcurl
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
            COMMAND ${CMAKE_COMMAND} -E echo ""
            COMMAND ${CMAKE_COMMAND} -E echo "-- Copying ${asset}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/${asset}"
                    "$<TARGET_FILE_DIR:uni_server>/${asset}"
        )
    else()
        message(WARNING "Runtime asset '${asset}' not found: it will not be copied next to the executable.")
    endif()
endforeach()

if(EXISTS "${CMAKE_SOURCE_DIR}/public")
    # Symlink rather than copy: a `copy_directory` snapshot only refreshes on
    # the next `cmake --build`, so `npm run watch` rebuilds (which write
    # straight into the root public/) never reached the binary's runtime
    # public/ without manually rebuilding/restarting the server. A symlink
    # means the running server always sees whatever's on disk at
    # ${CMAKE_SOURCE_DIR}/public live, no rebuild needed. `rm -rf` first
    # since create_symlink refuses to overwrite an existing path (e.g. a
    # leftover directory copy from before this change).
    add_custom_target(sync_public ALL
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "-- Symlinking public/ next to the executable"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:uni_server>/public"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${CMAKE_SOURCE_DIR}/public"
                "$<TARGET_FILE_DIR:uni_server>/public"
    )
    add_dependencies(sync_public uni_server)
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
    add_executable(uni_tests
        ${TEST_SOURCES}
        src/common/rate_limiter.cpp
        src/common/login_throttle.cpp
        src/common/lobby.cpp
        src/match/modload/mod_loader.cpp
        src/match/modload/semantic_validator.cpp
        src/match/modload/play_conditions.cpp
        src/match/modload/asset_index.cpp
        src/match/ecs/entity_store.cpp
        src/match/ecs/component_catalog.cpp
        src/match/ecs/event_bus.cpp
        src/match/engine/match_assembler.cpp
        src/match/engine/match_instance.cpp
        src/match/engine/play_evaluator.cpp
        src/match/engine/mutation_compiler.cpp
        src/match/view/event_sink.cpp
        src/match/view/defs_builder.cpp
        src/match/view/view_builder.cpp
        src/match/view/view_util.cpp
        src/match/server/match_session.cpp
        src/match/server/ready_barrier.cpp
        src/match/server/bot_policy.cpp
        src/match/server/stats_gate.cpp
        src/match/resolver.cpp
        src/match/rng.cpp
        src/match/scheduler.cpp
        src/match/status.cpp
        src/match/status_system.cpp
        src/match/content_wiring.cpp
        src/match/timers.cpp
        src/match/ops/default_ops.cpp
        src/match/ops/op_helpers.cpp
        src/match/ops/card_pile_ops.cpp
        src/match/ops/condition_ops.cpp
        src/match/ops/condition_eval.cpp
        src/match/ops/turn_flow_ops.cpp
        src/match/ops/status_ops.cpp
        src/match/ops/prompt_ops.cpp
        src/match/ops/random_ops.cpp
        src/match/ops/visibility_ops.cpp
        src/match/ops/restriction_ops.cpp
        src/match/ops/win_ops.cpp
        src/match/ops/meta_ops.cpp
        src/controllers/lobby_controller.cpp
        src/controllers/match_controller.cpp
        src/controllers/auth_controller.cpp
        src/controllers/password_reset_controller.cpp
        src/controllers/stats_controller.cpp
        src/controllers/friend_controller.cpp
        src/controllers/chat_controller.cpp
        src/services/auth_service.cpp
        src/services/verification_service.cpp
        src/services/password_reset_service.cpp
        src/services/account_reaper.cpp
        src/common/email_sender.cpp
        src/common/email_queue.cpp
        src/common/email_templates.cpp
        src/common/email_send_log.cpp
        src/common/crypto_hash.cpp
        src/services/stats_service.cpp
        src/services/friend_service.cpp
        src/services/chat_service.cpp
        src/action_router.cpp
        src/http_router.cpp
        src/common/http_utils.cpp
        src/database.cpp
        src/transport/uws_broadcaster.cpp
        src/transport/uws_timer_service.cpp
        src/transport/presence_registry.cpp
        src/webserver.cpp
    )
    target_include_directories(uni_tests PRIVATE include src "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(uni_tests PRIVATE
        doctest::doctest
        Threads::Threads
        nlohmann_json::nlohmann_json
        uwebsockets::uwebsockets
        usockets::usockets
        libuv::uv_a
        OpenSSL::SSL OpenSSL::Crypto
        SQLite::SQLite3
        ZLIB::ZLIB
        jwt-cpp::jwt-cpp
    )
    add_dependencies(uni_tests gen_ws_messages gen_contract_hpp gen_ws_actions_hpp gen_email_copy_hpp)

    add_test(NAME unit COMMAND uni_tests)
endif()

message(STATUS "Configuration complete for system: ${CMAKE_SYSTEM_NAME}")
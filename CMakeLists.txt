cmake_minimum_required(VERSION 3.15)
project(UniUno VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---
find_package(OpenSSL REQUIRED)

if(WIN32)
    # vcpkg on Windows provides a clean CMake config for libuv
    find_package(libuv CONFIG REQUIRED)
endif()

# --- uSockets (Explicit sources to avoid compiling Linux-only loops on Windows) ---
set(USOCKETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/uWebSockets/uSockets/src)
set(USOCKETS_SOURCES
    ${USOCKETS_DIR}/bsd.c
    ${USOCKETS_DIR}/context.c
    ${USOCKETS_DIR}/loop.c
    ${USOCKETS_DIR}/socket.c
    ${USOCKETS_DIR}/crypto/openssl.c
    ${USOCKETS_DIR}/crypto/sni_tree.cpp
    ${USOCKETS_DIR}/eventing/libuv.c
)

add_library(uSockets STATIC ${USOCKETS_SOURCES})
set_target_properties(uSockets PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(uSockets PUBLIC ${USOCKETS_DIR})
# Tell uSockets to use OpenSSL and libuv
target_compile_definitions(uSockets PUBLIC LIBUS_USE_OPENSSL LIBUS_USE_LIBUV)

# --- SQLite Amalgamation ---
add_library(sqlite3 STATIC external/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC external/sqlite3)
target_compile_options(sqlite3 PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/w>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
)
target_compile_definitions(sqlite3 PUBLIC SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=1)

# --- Main Executables ---
set(SOURCES 
    src/main.cpp 
    src/WebServer.cpp
)

add_executable(uno_server ${SOURCES})

target_include_directories(uno_server PRIVATE 
    external/uWebSockets/src
    external/jwt-cpp/include
    ${USOCKETS_DIR}
    external 
)

# --- Platform Specific Linking ---
if(WIN32)
    target_link_libraries(uno_server PRIVATE 
        uSockets 
        sqlite3
        OpenSSL::SSL OpenSSL::Crypto
        $<TARGET_NAME:libuv::uv>
        iphlpapi psapi user32 crypt32 ws2_32 advapi32 userenv
    )
else()
    target_link_libraries(uno_server PRIVATE 
        uSockets 
        sqlite3
        OpenSSL::SSL OpenSSL::Crypto
        uv
        pthread 
        z 
    )
endif()

message(STATUS "Configuration complete for system: ${CMAKE_SYSTEM_NAME}")

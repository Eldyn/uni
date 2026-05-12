cmake_minimum_required(VERSION 3.15)
project(UniUno VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Trova i pacchetti forniti da Conan ---
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(libuv REQUIRED)
find_package(uwebsockets REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(jwt-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)

# --- Main Executable ---
set(SOURCES src/main.cpp src/WebServer.cpp)
add_executable(uno_server ${SOURCES})

# Non serve più la cartella external. Conan instrada gli include in automatico.
# Aggiungi solo 'src' o 'include' se hai tuoi file header locali.
target_include_directories(uno_server PRIVATE src)

# --- Linking ---
# Usa esclusivamente i target esportati da Conan. 
# Questi target portano con sé tutte le flag (pthread, ws2_32, crypt32, ecc.)
target_link_libraries(uno_server PRIVATE 
    OpenSSL::SSL 
    OpenSSL::Crypto
    ZLIB::ZLIB
    libuv::uv_a
    uwebsockets::uwebsockets
    SQLite::SQLite3
    jwt-cpp::jwt-cpp
    nlohmann_json::nlohmann_json
)

message(STATUS "Configuration complete for system: ${CMAKE_SYSTEM_NAME}")

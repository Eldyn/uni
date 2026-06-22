#pragma once
#include <string>
#include <string_view>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <logger.hpp>

/**
 * @file env.hpp
 * @brief Management of environment variables and loading from the local `.env` configuration file.
 * * Supported file format:
 * - `KEY=VALUE` (Basic assignment)
 * - `KEY="VALUE"` (The surrounding double quotes are removed automatically)
 * - `# comment` (Lines starting with '#' are ignored)
 * - Empty lines are silently skipped
 * * All values are injected into the operating system's native process environment
 * (via `setenv` or `_putenv_s`), making them visible also to child processes or third-party
 * libraries that call `std::getenv()` directly.
 */

namespace Env {

/**
 * @brief Retrieves the value of a mandatory environment variable.
 * * To be used for fundamental values without which the server cannot function
 * correctly (e.g. `JWT_SECRET`). Failing fast at startup is
 * preferable to a cryptic error generated deep in the call stack.
 * * @param key The name of the environment variable to read.
 * @return std::string The value extracted from the operating environment.
 * @throws std::runtime_error If the required variable is not defined in the environment.
 * @tag CMN-ENV-MTH-001
 */
inline std::string Require(const std::string& key) {
    const char* val = std::getenv(key.c_str());
    if (!val) {
        throw std::runtime_error("[Env] Required variable '" + key + "' is not set");
    }
    return val;
}

/**
 * @brief Retrieves the value of an environment variable, providing an optional fallback.
 * * Safe to call even before `Load()` runs, since it interacts
 * directly with the system variables provided by the shell or the OS.
 * * @param key The name of the environment variable to read.
 * @param fallback The default value to return if the variable does not exist.
 * @return std::string The extracted value or the fallback string.
 * @tag CMN-ENV-MTH-002
 */
inline std::string Get(const std::string& key,
                        const std::string& fallback = "") {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : fallback;
}

/**
 * @brief Retrieves an integer environment variable, with a fallback on absence or parse error.
 * * Centralises the "read, try-parse, fall back" pattern so numeric tunables can
 * be declared in one line instead of a repeated try/catch around std::stoi.
 * * @param key The name of the environment variable to read.
 * @param fallback The default returned if the variable is missing or not an integer.
 * @return int The parsed value, or the fallback.
 * @tag CMN-ENV-MTH-002B
 */
inline int GetInt(const std::string& key, int fallback) {
    const char* val = std::getenv(key.c_str());
    if (!val) return fallback;
    try {
        return std::stoi(val);
    } catch (...) {
        return fallback;
    }
}

/**
 * @brief Internal helper function to set an environment variable in a cross-platform way.
 * * POSIX: Uses `setenv(key, value, 1)` where '1' forces overwriting if it already exists.
 * Windows: Uses `_putenv_s` which overwrites by default behaviour.
 * * @param key The name of the variable to set.
 * @param value The value to assign to the variable.
 * @tag CMN-ENV-MTH-003
 */
inline void SetEnv(const std::string& key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    ::setenv(key.c_str(), value.c_str(), 1);
#endif
}

/**
 * @brief Internal helper function to remove ASCII whitespace from the beginning and end of a string.
 * Removes the characters ' ', '\t', '\r', '\n'. Prevents undefined behaviour (UB) by returning
 * an empty string if the input string is composed entirely of whitespace.
 * * @param s The string to clean up.
 * @return std::string The trimmed string.
 * @tag CMN-ENV-MTH-004
 */
inline std::string Trim(const std::string& s) {
    constexpr std::string_view kWs = " \t\r\n";
    auto start = s.find_first_not_of(kWs);
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(kWs);
    return s.substr(start, end - start + 1);
}

/**
 * @brief Reads the file line by line and injects each KEY=VALUE pair into the process environment.
 * * Does not raise fatal errors if the file does not exist: modern production environments (Docker,
 * systemd, CI) often inject the environment variables directly without creating any `.env`
 * file on disk.
 * * @param path The path of the file to read (default: ".env").
 * @return int The exact number of environment variables read and injected successfully.
 * @tag CMN-ENV-MTH-005
 */
inline int Load(std::string_view path = ".env") {
    std::ifstream file(path.data());
    if (!file.is_open()) {
        Logger::Warn("[Env] .env not found at '" + std::string(path) +
                     "' — relying on existing environment variables");
        return 0;
    }

    int count = 0;
    std::string line;

    // std::getline extracts the characters up to '\\n', discarding the delimiter itself.
    while (std::getline(file, line)) {
        // Ignore empty lines and comments (first non-empty character is '#').
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;   // no '=' → malformed, skip

        std::string key   = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));

        // Remove any optional surrounding double quotes: "value" → value
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        if (key.empty()) continue;

        SetEnv(key, value);
        ++count;
    }

    Logger::Info("[Env] Loaded " + std::to_string(count) +
                 " variable(s) from '" + std::string(path) + "'");
    return count;
}

}

#pragma once
#include <string>
#include <string_view>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include "logger.hpp"

// ---------------------------------------------------------------------------
//  Env — tiny .env loader + typed accessor
// ---------------------------------------------------------------------------
//
//  File format (subset of the de-facto dotenv standard):
//    KEY=VALUE          plain assignment
//    # comment          lines starting with '#' are ignored
//    KEY="VALUE"        optional surrounding quotes are stripped
//    (blank lines)      silently skipped
//
//  Usage:
//    Env::Load(".env");                     // call once at program start
//    std::string s = Env::Require("JWT_SECRET");  // throws if missing
//    std::string s = Env::Get("PORT", "9999");    // default if missing
// ---------------------------------------------------------------------------
struct Env {
    // Load a .env file and push every key=value into the process environment.
    // Returns the number of variables successfully loaded.
    // Logs a warning if the file doesn't exist (non-fatal — prod envs often
    // inject variables directly without a file).
    static int Load(std::string_view path = ".env") {
        std::ifstream file(path.data());
        if (!file.is_open()) {
            Logger::Warn("[Env] .env file not found at '" + std::string(path) + "' — relying on existing environment variables");
            return 0;
        }

        int count = 0;
        std::string line;

        while (std::getline(file, line)) {          // read one line at a time
            // --- skip blanks and comments ---
            if (line.empty() || line[0] == '#') continue;

            // --- find the first '=' separator ---
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;  // malformed, skip

            std::string key   = Trim(line.substr(0, eq));
            std::string value = Trim(line.substr(eq + 1));

            // --- strip optional surrounding double-quotes ---
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

    // Return the environment variable, or throw std::runtime_error if absent.
    // Use for values that are mandatory (e.g. JWT_SECRET).
    static std::string Require(const std::string& key) {
        const char* val = std::getenv(key.c_str());  // POSIX + MSVC
        if (!val) {
            throw std::runtime_error("[Env] Required variable '" + key + "' is not set");
        }
        return val;
    }

    // Return the environment variable, or `fallback` if absent.
    static std::string Get(const std::string& key,
                           const std::string& fallback = "") {
        const char* val = std::getenv(key.c_str());
        return val ? std::string(val) : fallback;
    }

private:
    // Cross-platform setenv: POSIX has setenv(); Windows uses _putenv_s().
    static void SetEnv(const std::string& key, const std::string& value) {
#ifdef _WIN32
        _putenv_s(key.c_str(), value.c_str());
#else
        // overwrite=1 → replace if already set (allows .env to override)
        ::setenv(key.c_str(), value.c_str(), /*overwrite=*/1);
#endif
    }

    // Remove leading and trailing ASCII whitespace from a string.
    // std::string::find_first_not_of / find_last_not_of return npos when
    // the string is all whitespace, so we guard with an explicit empty check.
    static std::string Trim(const std::string& s) {
        const std::string ws = " \t\r\n";
        auto start = s.find_first_not_of(ws);
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(ws);
        // substr(pos, len): pos = first non-ws, len = last non-ws - first + 1
        return s.substr(start, end - start + 1);
    }
};

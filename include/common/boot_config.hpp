#pragma once
#include <common/env.hpp>
#include <stdexcept>
#include <string>

// INFO: Validates the process configuration at startup and throws on an unsafe
//       setup. Called from main() after Env::Load; unit-tested directly.
inline void ValidateBootConfigOrThrow() {
    if (Env::Get("JWT_SECRET", "").size() < 32) {
        throw std::runtime_error("[Config] JWT_SECRET must be set to at least 32 characters");
    }
    if (Env::Get("PASSWORD_PEPPER", "").empty()) {
        throw std::runtime_error("[Config] PASSWORD_PEPPER must be set");
    }
    const std::string mode = Env::Get("EMAIL_MODE", "dev");
    if (mode != "dev" && mode != "live") {
        throw std::runtime_error("[Config] EMAIL_MODE must be 'dev' or 'live'");
    }
    if (mode == "live" && Env::Get("EMAIL_KEY", "").empty()) {
        throw std::runtime_error("[Config] EMAIL_MODE=live requires EMAIL_KEY");
    }
    if (Env::Get("UNI_ENV", "development") == "production" && mode != "live") {
        throw std::runtime_error("[Config] Production requires EMAIL_MODE=live");
    }
}

#pragma once

#include <common/env.hpp>

/**
 * @file environment.hpp
 * @brief The single answer to "is this a production process?".
 *
 * A build with `UNI_PROD_BUILD` is always production. Any other build is
 * production when `UNI_ENV` is `production`. Code that must refuse a
 * development-only feature asks here instead of reading the variables itself.
 */

inline bool IsProductionEnvironment() {
#ifdef UNI_PROD_BUILD
    return true;
#else
    return Env::Get("UNI_ENV", "development") == "production";
#endif
}

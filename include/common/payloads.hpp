#pragma once
// Payload structs are generated from contract/asyncapi.yaml by hatchbed/asyncapi_gencpp.
// Regenerate: cmake --build <build_dir> --target gen_ws_messages
#include <ws/messages.h>
#include <result.hpp>

namespace ws {

// INFO: ParsePayload<T> wraps the hatchbed-generated T::fromJson() into the
//       project's monadic Result<T> type so all call sites remain uniform.
//       fromJson only type-checks, so isValid() is what enforces the contract
//       limits (lengths, ranges); a wrong-typed field surfaces as a
//       json::exception, which is treated as a malformed payload.
template <typename T>
inline Result<T> ParsePayload(const nlohmann::json& json) {
    try {
        auto opt = T::fromJson(json);
        if (!opt || !opt->isValid()) {
            return std::unexpected(::Error::InvalidInput("Malformed or incomplete payload"));
        }
        return std::move(*opt);
    } catch (const nlohmann::json::exception&) {
        return std::unexpected(::Error::InvalidInput("Malformed or incomplete payload"));
    }
}

}  // namespace ws

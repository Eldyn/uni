#pragma once
// Payload structs are generated from contract/asyncapi.yaml by hatchbed/asyncapi_gencpp.
// Regenerate: cmake --build <build_dir> --target gen_ws_messages
#include <ws/messages.h>
#include <result.hpp>

namespace ws {

// INFO: ParsePayload<T> wraps the hatchbed-generated T::fromJson() into the
//       project's monadic Result<T> type so all call sites remain uniform.
template <typename T>
inline Result<T> ParsePayload(const nlohmann::json& json) {
    auto opt = T::fromJson(json);
    if (!opt) {
        return std::unexpected(::Error::InvalidInput("Malformed or incomplete payload"));
    }
    return std::move(*opt);
}

}  // namespace ws

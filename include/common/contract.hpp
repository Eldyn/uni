#pragma once

// Validation constants — single source of truth is contract/asyncapi.yaml.
// These values are kept in sync manually; the frontend derives them via
// the generate:contract script.
namespace contract {
    inline constexpr int kUsernameMin    = 3;
    inline constexpr int kUsernameMax    = 32;
    inline constexpr int kPasswordMin    = 8;
    inline constexpr int kLobbyNameMax   = 50;
    inline constexpr int kLobbyCodeLen   = 6;
    inline constexpr int kMaxLobbyMembers = 10;
}  // namespace contract

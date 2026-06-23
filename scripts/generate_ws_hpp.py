#!/usr/bin/env python3
"""
Generates the C++ WebSocket actions header (namespace ``ws``) from
contract/asyncapi.yaml.

Reads the channel publish/subscribe message lists and emits:
  * ClientAction — struct of ``static constexpr const char*`` constants, one
    per client→server action (channel publish list).
  * ServerAction — enum class, one enumerator per server→client action
    (channel subscribe list).
  * kServerActionStr — inline unordered_map<ServerAction, std::string> for
    enum→wire-string conversion used by MakeResponse() and SendError().

Adding a new action is a contract-only edit; this script does not change.
It is the C++ counterpart of the ClientAction/ServerAction blocks in
frontend/scripts/generate-schemas.js.

Usage: generate_ws_hpp.py <asyncapi.yaml> <output.hpp>
"""
import os
import sys

import yaml


def die(msg):
    sys.stderr.write(f"[generate-ws-hpp] error: {msg}\n")
    sys.exit(1)


def to_k_pascal(action_str):
    """'lobby_create' -> 'kLobbyCreate'"""
    return "k" + "".join(w.capitalize() for w in action_str.split("_"))


def collect_actions(data):
    """Return (client_actions, server_actions) as ordered lists of (k_name, wire_str)."""
    messages = (data.get("components") or {}).get("messages") or {}
    channels = data.get("channels") or {}

    client, server = [], []
    seen_client, seen_server = set(), set()

    for ch in channels.values():
        for ref in (ch.get("publish") or {}).get("message", {}).get("oneOf") or []:
            name = (ref.get("$ref") or "").split("/")[-1]
            action_str = (messages.get(name) or {}).get("name")
            if action_str and action_str not in seen_client:
                seen_client.add(action_str)
                client.append((to_k_pascal(action_str), action_str))

        for ref in (ch.get("subscribe") or {}).get("message", {}).get("oneOf") or []:
            name = (ref.get("$ref") or "").split("/")[-1]
            action_str = (messages.get(name) or {}).get("name")
            if action_str and action_str not in seen_server:
                seen_server.add(action_str)
                server.append((to_k_pascal(action_str), action_str))

    return client, server


def main():
    if len(sys.argv) != 3:
        die("usage: generate_ws_hpp.py <asyncapi.yaml> <output.hpp>")
    src, out = sys.argv[1], sys.argv[2]

    with open(src, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    client_actions, server_actions = collect_actions(data)

    if not client_actions:
        die("no client→server actions found in channel publish list")
    if not server_actions:
        die("no server→client actions found in channel subscribe list")

    width = max(len(k) for k, _ in client_actions)

    lines = [
        "#pragma once",
        "",
        "// AUTO-GENERATED — do not edit by hand.",
        "// Source:     contract/asyncapi.yaml (channels.publish / channels.subscribe)",
        "// Generator:  scripts/generate_ws_hpp.py (run by CMake target gen_ws_actions_hpp)",
        "//",
        "// Same single source of truth the frontend derives via",
        "// scripts/generate-schemas.js → ClientAction / ServerAction.",
        "",
        "#include <cstdint>",
        "#include <string>",
        "#include <unordered_map>",
        "",
        "namespace ws {",
        "    /**",
        "     * @struct ClientAction",
        "     * @brief String constants for every action sent from Clients to the Server.",
        "     * @tag WS-STR-001",
        "     */",
        "    struct ClientAction {",
    ]

    for k_name, wire in client_actions:
        lines.append(f'        static constexpr const char* {k_name.ljust(width)} = "{wire}";')

    lines += [
        "",
        "        ClientAction() = delete;",
        "    };",
        "",
        "    /**",
        "     * @enum ServerAction",
        "     * @brief Actions sent from the Server to the connected Clients.",
        "     * @tag WS-ENUM-001",
        "     */",
        "    enum class ServerAction : uint8_t {",
    ]

    for k_name, _ in server_actions:
        lines.append(f"        {k_name},")

    lines += [
        "    };",
        "",
        "    inline const std::unordered_map<ServerAction, std::string> kServerActionStr {",
    ]

    for k_name, wire in server_actions:
        lines.append(f'        {{ ServerAction::{k_name}, "{wire}" }},')

    lines += [
        "    };",
        "}  // namespace ws",
        "",
    ]

    text = "\n".join(lines)

    try:
        with open(out, "r", encoding="utf-8") as f:
            if f.read() == text:
                return
    except FileNotFoundError:
        pass

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"[generate-ws-hpp] Written {out}")


if __name__ == "__main__":
    main()

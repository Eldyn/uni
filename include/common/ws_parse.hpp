#pragma once
// Zero-alloc WS inbound pre-parse (backend-perf Task 7).
//
// Scans the raw uWS frame for the top-level "action" / "request_id" string
// spans WITHOUT building a JSON DOM (no json::parse, no std::string). The
// returned views point into the caller's frame buffer and are valid only for
// the frame's lifetime (i.e. the OnSocketMessage callback scope).
//
// Semantics vs the old json::parse path:
//  - Missing / non-string / unterminated "action"  -> empty action (drop).
//  - Duplicate keys: LAST string occurrence wins (matches nlohmann).
//  - Escaped keys (e.g. "act\u0069on") do NOT match and the frame is
//    dropped; action/request_id values containing escapes are returned raw
//    (still routed correctly: the hashed Dispatch verifies against the
//    decoded DOM string and falls back on mismatch).
#include <cstdint>
#include <string_view>

// Views into the inbound uWS frame. Empty `action` means malformed/drop.
struct Inbound {
    std::string_view action;
    std::string_view request_id;
};

// FNV-1a 32-bit over the raw bytes. Empty input yields the offset basis.
inline uint32_t Fnv1a(std::string_view s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= static_cast<uint32_t>(c);
        h *= 16777619u;
    }
    return h;
}

// Hashed-dispatch key for an action name. Alias kept because the plan names
// this interface `ActionHash()` while the implementation is FNV-1a.
inline uint32_t ActionHash(std::string_view action) { return Fnv1a(action); }

namespace ws_parse_detail {
inline bool IsWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Scan `msg` for `"key" <ws>* : <ws>* "string-value"` occurrences, returning
// the inner span of the LAST string value (nlohmann keeps the last dup key).
// Occurrences whose value is not a string clear the result (last wins, and a
// trailing non-string value means "invalid action" on the old path too).
// Never allocates; never throws.
inline std::string_view ScanKey(std::string_view msg, std::string_view key) {
    std::string_view last;
    std::size_t pos = 0;
    while (pos < msg.size()) {
        const std::size_t q = msg.find('"', pos);
        if (q == std::string_view::npos) break;
        pos = q + 1;
        // Match `"key"` exactly at q.
        if (msg.size() - q - 1 < key.size() + 1) continue;
        if (msg.compare(q + 1, key.size(), key) != 0) continue;
        if (msg[q + 1 + key.size()] != '"') continue;
        std::size_t p = q + 1 + key.size() + 1;
        while (p < msg.size() && IsWs(msg[p])) ++p;
        if (p >= msg.size() || msg[p] != ':') continue;
        ++p;
        while (p < msg.size() && IsWs(msg[p])) ++p;
        if (p >= msg.size()) {
            last = {};
            continue;
        }
        if (msg[p] != '"') {
            last = {};  // key present, value not a string -> invalid
            pos = p + 1;
            continue;
        }
        const std::size_t v0 = p + 1;
        std::size_t v1 = v0;
        bool terminated = false;
        while (v1 < msg.size()) {
            const char c = msg[v1];
            if (c == '\\') {
                v1 += 2;
                continue;
            }
            if (c == '"') {
                terminated = true;
                break;
            }
            ++v1;
        }
        if (!terminated) return {};  // truncated frame -> malformed, drop
        last = msg.substr(v0, v1 - v0);
        pos = v1 + 1;
    }
    return last;
}
}  // namespace ws_parse_detail

// Extract `action` + `request_id` spans from a raw WS frame. `request_id`
// is only scanned when `action` is present (drop path skips it entirely).
inline Inbound ParseInbound(std::string_view message) {
    Inbound in;
    in.action = ws_parse_detail::ScanKey(message, "action");
    if (!in.action.empty()) in.request_id = ws_parse_detail::ScanKey(message, "request_id");
    return in;
}

#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <websocket_context.hpp>

namespace http {

//  Accumulates a full HTTP request body from uWS's chunked onData delivery,
//  then invokes `callback` with the complete body string.
//
//  Why this is non-trivial in uWS:
//    uWS never blocks. Bodies arrive in one or more string_view chunks via
//    the onData callback. The bool `isLast` signals the final chunk. Callers
//    must therefore buffer every chunk themselves and act only when isLast
//    is true — which is exactly what this helper does.
//
//  The shared_ptr<bool> is_alive guard handles connection abortion:
//    If the client disconnects mid-transfer, uWS fires onAborted and the
//    `res` pointer becomes dangling. The guard flips to false inside
//    onAborted so that any subsequent onData invocation (which can race on
//    some platforms) detects the abort and returns immediately without
//    touching `res`.
//
//  @param res        The response object for the current HTTP request.
//                    Must remain valid until `callback` is invoked or the
//                    connection is aborted — uWS guarantees this.
//  @param max_bytes  Hard cap on body size. Responds 413 and stops if
//                    exceeded. Callers should pass their domain-specific
//                    limit rather than relying on a global default.
//  @param callback   Invoked exactly once with the complete body, or never
//                    if the connection is aborted or the size cap is hit.
inline void ReadBody(AppResponse* res, size_t max_bytes, std::function<void(const std::string&)> callback) {
    auto is_alive = std::make_shared<bool>(true);
    auto buffer   = std::make_shared<std::string>();

    res->onAborted([is_alive] { *is_alive = false; });

    res->onData([is_alive, buffer, max_bytes, callback = std::move(callback), res](std::string_view chunk, bool isLast) mutable {
        if (!*is_alive) return;

        buffer->append(chunk.data(), chunk.size());

        if (buffer->size() > max_bytes) {
            res->writeStatus("413 Payload Too Large")->end();
            return;
        }

        if (isLast) callback(*buffer);
    });
}

constexpr std::string_view TrimWhitespace(std::string_view str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

inline std::optional<std::string> GetCookieValue(std::string_view cookie_header, std::string_view target_key) {
    size_t start = 0;
    
    while (start < cookie_header.size()) {
        size_t end = cookie_header.find(';', start);
        
        std::string_view pair = (end == std::string_view::npos) 
                                ? cookie_header.substr(start) 
                                : cookie_header.substr(start, end - start);

        // Split the pair into key and value
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string_view::npos) {
            std::string_view key = TrimWhitespace(pair.substr(0, eq_pos));
            
            if (key == target_key) {
                return std::string(TrimWhitespace(pair.substr(eq_pos + 1)));
            }
        }

        if (end == std::string_view::npos) {
            break;
        }
        
        start = end + 1;
    }

    return "";
}

}

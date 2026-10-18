#include <controllers/avatar_controller.hpp>

#include <avatar/loadout.hpp>
#include <common/http.hpp>
#include <services/auth_service.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/**
 * @file avatar_controller.cpp
 * @brief Wire glue for the loadout API: resolve the session, then delegate the
 *        rules to `uni::avatar::ValidateLoadout` and the row to
 *        `LoadoutRepository`. The body is capped before it reaches the rules.
 */

using json = nlohmann::json;

namespace {

/** @brief The documented body limit for a `PUT` loadout. */
constexpr std::size_t kMaxLoadoutBytes = 8 * 1024;

// INFO: Transport cap above kMaxLoadoutBytes so the handler owns the 8 KiB
//       rule and answers 400; ReadBody's own cap only stops runaway uploads
//       (413).
constexpr std::size_t kReadBodyCapBytes = 64 * 1024;

/** @brief Guests are the only identities whose name carries a '#'. */
bool IsGuest(const std::string& username) {
    return username.starts_with("Guest#");
}

/** @brief The stable per-session seed for a guest name (FNV-1a, 32-bit). */
uint32_t GuestSeed(const std::string& username) {
    uint32_t hash = 2166136261u;
    for (const char raw : username) {
        hash ^= static_cast<unsigned char>(raw);
        hash *= 16777619u;
    }
    return hash;
}

/** @brief Session token: ws_token first (guests, embeds), then auth_token. */
std::optional<std::string> SessionToken(AppRequest* req) {
    std::string_view cookies = req->getHeader("cookie");
    auto token = http::GetCookieValue(cookies, "ws_token");
    if (!token || token->empty()) {
        token = http::GetCookieValue(cookies, "auth_token");
    }
    if (!token || token->empty()) return std::nullopt;
    return token;
}

/** @brief Write `{ "loadout": <loadout> }` with a 200. */
void WriteLoadout(AppResponse* res, const json& loadout) {
    res->writeHeader("Content-Type", "application/json")
       ->end(json({{"loadout", loadout}}).dump());
}

/** @brief Write `{ "error": code, "detail": detail }` with `status`. */
void WriteError(AppResponse* res, const char* status, const std::string& code,
                const std::string& detail) {
    res->writeStatus(status)
       ->writeHeader("Content-Type", "application/json")
       ->end(json({{"error", code}, {"detail", detail}}).dump());
}

}  // namespace

AvatarController::AvatarController(HttpRouter& router,
                                   const uni::avatar::Catalogue& catalogue)
    : catalogue_(catalogue), ownership_(catalogue) {
    router.Get("/avatar/loadout/me", [this](AppResponse* res, AppRequest* req) {
        HandleGetMe(res, req);
    });

    router.Put("/avatar/loadout/me", [this](AppResponse* res, AppRequest* req) {
        HandlePutMe(res, req);
    });
}

void AvatarController::HandleGetMe(AppResponse* res, AppRequest* req) {
    const std::optional<std::string> token = SessionToken(req);
    if (!token) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    auto payload = AuthService::VerifyToken(*token);
    if (!payload) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    if (IsGuest(payload->username)) {
        WriteLoadout(res, uni::avatar::GuestLoadout(
                              catalogue_, GuestSeed(payload->username)));
        return;
    }

    auto stored = repository_.Get(payload->username);
    if (stored) {
        WriteLoadout(res, *stored);
        return;
    }

    // INFO: seed 0 picks the first basic swatch, the documented default look.
    WriteLoadout(res, uni::avatar::GuestLoadout(catalogue_, 0));
}

void AvatarController::HandlePutMe(AppResponse* res, AppRequest* req) {
    const std::optional<std::string> token = SessionToken(req);
    if (!token) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    auto payload = AuthService::VerifyToken(*token);
    if (!payload) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    if (IsGuest(payload->username)) {
        WriteError(res, "403 Forbidden", "guest", "guests cannot save a loadout");
        return;
    }

    const std::string username = payload->username;
    http::ReadBody(res, kReadBodyCapBytes,
                   [this, res, username](const std::string& body) {
        if (body.size() > kMaxLoadoutBytes) {
            WriteError(res, "400 Bad Request", "bad_json", "loadout body too large");
            return;
        }

        const json loadout = json::parse(body, nullptr, false);
        if (loadout.is_discarded() || !loadout.is_object()) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "loadout must be a JSON object");
            return;
        }

        const std::optional<uni::avatar::LoadoutError> error =
            uni::avatar::ValidateLoadout(loadout, catalogue_, ownership_, username);
        if (error) {
            WriteError(res, "400 Bad Request", error->code, error->detail);
            return;
        }

        if (!repository_.Save(username, loadout)) {
            WriteError(res, "500 Internal Server Error", "internal",
                       "could not save the loadout");
            return;
        }

        if (on_saved) on_saved(username, loadout);
        WriteLoadout(res, loadout);
    });
}

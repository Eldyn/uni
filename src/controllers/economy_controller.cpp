#include <controllers/economy_controller.hpp>

#include <common/http.hpp>
#include <services/auth_service.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file economy_controller.cpp
 * @brief Wire glue for the economy API: resolve the session, then delegate the
 *        rules to `uni::economy::EconomyService`. The buy body is capped
 *        before it reaches the service.
 */

using json = nlohmann::json;

namespace {

/** @brief The documented body limit for a `POST /shop/buy`. */
constexpr std::size_t kMaxBuyBytes = 8 * 1024;

// INFO: Transport cap above kMaxBuyBytes so the handler owns the 8 KiB rule
//       and answers 400; ReadBody's own cap only stops runaway uploads (413).
constexpr std::size_t kReadBodyCapBytes = 64 * 1024;

/** @brief Guests are the only identities whose name carries a '#'. */
bool IsGuest(const std::string& username) {
    return username.starts_with("Guest#");
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

/** @brief Write `{ "error": code, "detail": detail }` with `status`. */
void WriteError(AppResponse* res, const char* status, const std::string& code,
                const std::string& detail) {
    res->writeStatus(status)
       ->writeHeader("Content-Type", "application/json")
       ->end(json({{"error", code}, {"detail", detail}}).dump());
}

/** @brief One ledger row as the read API serializes it. */
json LedgerJson(const uni::economy::LedgerRow& row) {
    return json{{"amount", row.amount},
                {"source", row.source},
                {"ref", row.ref},
                {"created_at", row.created_at}};
}

/** @brief The status, code and detail a `BuyStatus` maps to on the wire. */
struct BuyError {
    const char* status;
    const char* code;
    const char* detail;
};

/** @brief Map a non-`kOk` buy status; `kOk` is handled by the caller. */
BuyError BuyErrorFor(uni::economy::BuyStatus status) {
    using uni::economy::BuyStatus;
    switch (status) {
        case BuyStatus::kInsufficient:
            return {"402 Payment Required", "insufficient_cash",
                    "not enough cash for this item"};
        case BuyStatus::kUnknownItem:
            return {"404 Not Found", "unknown_item",
                    "no such item in the catalogue"};
        case BuyStatus::kAlreadyOwned:
            return {"409 Conflict", "already_owned",
                    "the item is already owned"};
        case BuyStatus::kNotAvailable:
            return {"410 Gone", "not_available",
                    "the item is not for sale right now"};
        case BuyStatus::kNoAccount:
            return {"403 Forbidden", "guest", "guests cannot buy items"};
        case BuyStatus::kOk:
            break;
    }
    return {"500 Internal Server Error", "internal", "unknown buy status"};
}

}  // namespace

EconomyController::EconomyController(HttpRouter& router,
                                     uni::economy::EconomyService& economy)
    : economy_(economy) {
    router.Get("/economy/me", [this](AppResponse* res, AppRequest* req) {
        HandleGetMe(res, req);
    });

    router.Post("/shop/buy", [this](AppResponse* res, AppRequest* req) {
        HandleBuy(res, req);
    });
}

void EconomyController::HandleGetMe(AppResponse* res, AppRequest* req) {
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
        WriteError(res, "403 Forbidden", "guest", "guests have no balance");
        return;
    }

    const std::optional<std::int64_t> balance = economy_.Balance(payload->username);
    if (!balance) {
        // INFO: A verified token with no `users` row is treated like a guest:
        //       there is nothing to read and no balance to spend.
        WriteError(res, "403 Forbidden", "guest", "guests have no balance");
        return;
    }

    json owned = json::array();
    for (const std::string& id : economy_.Owned(payload->username)) {
        owned.push_back(id);
    }

    json ledger = json::array();
    for (const uni::economy::LedgerRow& row :
         economy_.Recent(payload->username, 50)) {
        ledger.push_back(LedgerJson(row));
    }

    res->writeHeader("Content-Type", "application/json")
       ->end(json({{"balance", *balance}, {"owned", owned}, {"ledger", ledger}})
                 .dump());
}

void EconomyController::HandleBuy(AppResponse* res, AppRequest* req) {
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
        WriteError(res, "403 Forbidden", "guest", "guests cannot buy items");
        return;
    }

    const std::string username = payload->username;
    http::ReadBody(res, kReadBodyCapBytes,
                   [this, res, username](const std::string& body) {
        if (body.size() > kMaxBuyBytes) {
            WriteError(res, "400 Bad Request", "bad_json", "buy body too large");
            return;
        }

        const json request = json::parse(body, nullptr, false);
        if (request.is_discarded() || !request.is_object() ||
            !request.contains("item") || !request["item"].is_string() ||
            !request.contains("request_id") ||
            !request["request_id"].is_string()) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "body must be {item, request_id}");
            return;
        }

        const std::string item = request["item"].get<std::string>();
        const std::string request_id = request["request_id"].get<std::string>();
        if (item.empty() || request_id.empty()) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "item and request_id must be non-empty");
            return;
        }

        const uni::economy::BuyResult result = economy_.Buy(
            username, item, request_id, static_cast<std::int64_t>(std::time(nullptr)));
        if (result.status != uni::economy::BuyStatus::kOk) {
            const BuyError error = BuyErrorFor(result.status);
            WriteError(res, error.status, error.code, error.detail);
            return;
        }

        res->writeHeader("Content-Type", "application/json")
           ->end(json({{"balance", result.balance}, {"item", item}}).dump());
    });
}

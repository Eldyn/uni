#include <controllers/economy_controller.hpp>

#include <common/http.hpp>
#include <services/auth_service.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

/**
 * @file economy_controller.cpp
 * @brief Wire glue for the economy API: resolve the session, then delegate the
 *        rules to `uni::economy::EconomyService`. Every write body is capped
 *        before it reaches the service.
 */

using json = nlohmann::json;

namespace {

/** @brief The documented body limit for a `POST /shop/buy`. */
constexpr std::size_t kMaxBuyBytes = 8 * 1024;

/** @brief The same 8 KiB rule for the wishlist and cosmetic writes. */
constexpr std::size_t kMaxWriteBytes = 8 * 1024;

// INFO: Transport cap above the documented body limits so the handler owns the
//       8 KiB rule and answers 400; ReadBody's own cap only stops runaway
//       uploads (413).
constexpr std::size_t kReadBodyCapBytes = 64 * 1024;

/** @brief The slots a user can equip a cosmetic in. */
constexpr std::array<std::string_view, 3> kCosmeticSlots = {
    "card_face", "card_back", "playmat"};

/** @brief Guests are the only identities whose name carries a '#'. */
bool IsGuest(const std::string& username) {
    return username.starts_with("Guest#");
}

/** @brief True for one of `card_face`, `card_back` and `playmat`. */
bool IsCosmeticSlot(std::string_view slot) {
    return std::find(kCosmeticSlots.begin(), kCosmeticSlots.end(), slot) !=
           kCosmeticSlots.end();
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

/**
 * @brief Resolve the session to a non-guest account, or write 401/403.
 *
 * A verified token without a `users` row is treated like a guest: there is
 * nothing to read and nothing to write for it.
 */
std::optional<std::string> RequireAccount(
    AppResponse* res, AppRequest* req, const uni::economy::EconomyService& economy,
    const char* guest_detail) {
    const std::optional<std::string> token = SessionToken(req);
    if (!token) {
        res->writeStatus("401 Unauthorized")->end();
        return std::nullopt;
    }

    auto payload = AuthService::VerifyToken(*token);
    if (!payload) {
        res->writeStatus("401 Unauthorized")->end();
        return std::nullopt;
    }

    if (IsGuest(payload->username) || !economy.Balance(payload->username)) {
        WriteError(res, "403 Forbidden", "guest", guest_detail);
        return std::nullopt;
    }
    return payload->username;
}

/** @brief Read a capped `{ item }` body and hand the id to `next`. */
void ReadItemBody(AppResponse* res,
                  std::function<void(const std::string&)> next) {
    http::ReadBody(res, kReadBodyCapBytes,
                   [res, next = std::move(next)](const std::string& body) {
        if (body.size() > kMaxWriteBytes) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "wishlist body too large");
            return;
        }

        const json request = json::parse(body, nullptr, false);
        if (request.is_discarded() || !request.is_object() ||
            !request.contains("item") || !request["item"].is_string()) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "body must be {item}");
            return;
        }

        const std::string item = request["item"].get<std::string>();
        if (item.empty()) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "item must be non-empty");
            return;
        }
        next(item);
    });
}

/** @brief Read a capped `{ slot, item }` body and hand both to `next`. */
void ReadCosmeticBody(
    AppResponse* res,
    std::function<void(const std::string&, const std::string&)> next) {
    http::ReadBody(res, kReadBodyCapBytes,
                   [res, next = std::move(next)](const std::string& body) {
        if (body.size() > kMaxWriteBytes) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "cosmetic body too large");
            return;
        }

        const json request = json::parse(body, nullptr, false);
        if (request.is_discarded() || !request.is_object() ||
            !request.contains("slot") || !request["slot"].is_string() ||
            !request.contains("item") || !request["item"].is_string()) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "body must be {slot, item}");
            return;
        }

        const std::string slot = request["slot"].get<std::string>();
        const std::string item = request["item"].get<std::string>();
        if (slot.empty() || item.empty()) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "slot and item must be non-empty");
            return;
        }
        if (!IsCosmeticSlot(slot)) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "slot must be card_face, card_back or playmat");
            return;
        }
        next(slot, item);
    });
}

/** @brief The caller's wishlist as the API serializes it. */
json WishlistJson(const uni::economy::EconomyService& economy,
                  const std::string& username) {
    json items = json::array();
    for (const std::string& id : economy.Wishlist(username)) {
        items.push_back(id);
    }
    return json{{"items", items}};
}

/** @brief The three cosmetic slots, `null` for the default look. */
json CosmeticSlotsJson(const uni::economy::EconomyService& economy,
                       const std::string& username) {
    json slots = json::object();
    for (std::string_view slot : kCosmeticSlots) {
        const std::optional<std::string> item =
            economy.Cosmetic(username, std::string(slot));
        slots[std::string(slot)] = item ? json(*item) : json(nullptr);
    }
    return json{{"slots", slots}};
}

/** @brief One decimal query parameter, or `fallback` when absent/invalid. */
int QueryInt(AppRequest* req, std::string_view key, int fallback) {
    const std::string_view raw = req->getQuery(key);
    if (raw.empty()) return fallback;

    int value = 0;
    const auto [ptr, ec] =
        std::from_chars(raw.data(), raw.data() + raw.size(), value);
    if (ec != std::errc{} || ptr != raw.data() + raw.size()) return fallback;
    return value;
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
                                     uni::economy::EconomyService& economy,
                                     const uni::avatar::Catalogue& catalogue)
    : economy_(economy), catalogue_(catalogue) {
    router.Get("/economy/me", [this](AppResponse* res, AppRequest* req) {
        HandleGetMe(res, req);
    });

    router.Post("/shop/buy", [this](AppResponse* res, AppRequest* req) {
        HandleBuy(res, req);
    });

    router.Get("/shop/wishlist", [this](AppResponse* res, AppRequest* req) {
        HandleGetWishlist(res, req);
    });

    router.Post("/shop/wishlist", [this](AppResponse* res, AppRequest* req) {
        HandleAddWishlist(res, req);
    });

    router.Delete("/shop/wishlist", [this](AppResponse* res, AppRequest* req) {
        HandleRemoveWishlist(res, req);
    });

    router.Get("/shop/popular", [this](AppResponse* res, AppRequest* req) {
        HandlePopular(res, req);
    });

    router.Get("/cosmetics/me", [this](AppResponse* res, AppRequest* req) {
        HandleGetCosmetics(res, req);
    });

    router.Put("/cosmetics/me", [this](AppResponse* res, AppRequest* req) {
        HandlePutCosmetics(res, req);
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

void EconomyController::HandleGetWishlist(AppResponse* res, AppRequest* req) {
    const std::optional<std::string> username =
        RequireAccount(res, req, economy_, "guests cannot use the wishlist");
    if (!username) return;

    res->writeHeader("Content-Type", "application/json")
       ->end(WishlistJson(economy_, *username).dump());
}

void EconomyController::HandleAddWishlist(AppResponse* res, AppRequest* req) {
    const std::optional<std::string> username =
        RequireAccount(res, req, economy_, "guests cannot use the wishlist");
    if (!username) return;

    ReadItemBody(res, [this, res, username = *username](
                          const std::string& item) {
        const uni::avatar::Item* entry = catalogue_.FindItem(item);
        if (entry == nullptr) {
            WriteError(res, "404 Not Found", "unknown_item",
                       "no such item in the catalogue");
            return;
        }
        if (entry->hidden) {
            WriteError(res, "404 Not Found", "not_available",
                       "the item is hidden");
            return;
        }
        if (!economy_.AddWishlist(username, item)) {
            WriteError(res, "500 Internal Server Error", "internal",
                       "could not update the wishlist");
            return;
        }
        res->writeHeader("Content-Type", "application/json")
           ->end(WishlistJson(economy_, username).dump());
    });
}

void EconomyController::HandleRemoveWishlist(AppResponse* res, AppRequest* req) {
    const std::optional<std::string> username =
        RequireAccount(res, req, economy_, "guests cannot use the wishlist");
    if (!username) return;

    ReadItemBody(res, [this, res, username = *username](
                          const std::string& item) {
        if (catalogue_.FindItem(item) == nullptr) {
            WriteError(res, "404 Not Found", "unknown_item",
                       "no such item in the catalogue");
            return;
        }
        if (!economy_.RemoveWishlist(username, item)) {
            WriteError(res, "500 Internal Server Error", "internal",
                       "could not update the wishlist");
            return;
        }
        res->writeHeader("Content-Type", "application/json")
           ->end(WishlistJson(economy_, username).dump());
    });
}

void EconomyController::HandlePopular(AppResponse* res, AppRequest* req) {
    const std::optional<std::string> username =
        RequireAccount(res, req, economy_, "guests cannot browse the shop");
    if (!username) return;

    const int days = std::clamp(QueryInt(req, "days", 7), 1, 90);
    const int limit = std::clamp(QueryInt(req, "limit", 12), 1, 50);

    json items = json::array();
    for (const uni::economy::PopularItem& row : economy_.Popular(days, limit)) {
        items.push_back(json{{"item", row.item_id},
                             {"purchases", row.purchases},
                             {"wishlists", row.wishlists}});
    }
    res->writeHeader("Content-Type", "application/json")
       ->end(json({{"days", days}, {"items", items}}).dump());
}

void EconomyController::HandleGetCosmetics(AppResponse* res, AppRequest* req) {
    const std::optional<std::string> username =
        RequireAccount(res, req, economy_, "guests cannot equip cosmetics");
    if (!username) return;

    res->writeHeader("Content-Type", "application/json")
       ->end(CosmeticSlotsJson(economy_, *username).dump());
}

void EconomyController::HandlePutCosmetics(AppResponse* res, AppRequest* req) {
    const std::optional<std::string> username =
        RequireAccount(res, req, economy_, "guests cannot equip cosmetics");
    if (!username) return;

    ReadCosmeticBody(res, [this, res, username = *username](
                              const std::string& slot, const std::string& item) {
        const uni::avatar::Item* entry = catalogue_.FindItem(item);
        if (entry == nullptr) {
            WriteError(res, "404 Not Found", "unknown_item",
                       "no such item in the catalogue");
            return;
        }
        if (entry->slot != slot) {
            WriteError(res, "400 Bad Request", "bad_json",
                       "the item does not fit that slot");
            return;
        }
        if (!economy_.OwnsItem(username, item)) {
            WriteError(res, "404 Not Found", "not_owned",
                       "the item is not owned");
            return;
        }
        if (!economy_.SetCosmetic(username, slot, item)) {
            WriteError(res, "500 Internal Server Error", "internal",
                       "could not equip the cosmetic");
            return;
        }
        res->writeHeader("Content-Type", "application/json")
           ->end(CosmeticSlotsJson(economy_, username).dump());
    });
}

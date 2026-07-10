#include <services/chat_service.hpp>
#include <common/crypto.hpp>

ChatService::ChatService(Database& db) : db_(db), dm_key_(Crypto::LoadKey()) {}

void ChatService::PostGlobalMessage(const std::string& username, const std::string& message) {
    global_history_.push_back({username, message});
    if (global_history_.size() > kGlobalHistoryLimit) {
        global_history_.pop_front();
    }
}

const std::deque<ChatMessageEntry>& ChatService::GetGlobalHistory() const {
    return global_history_;
}

VoidResult ChatService::SendDirectMessage(const std::string& sender,
                                          const std::string& recipient,
                                          const std::string& plaintext) {
    auto blob = Crypto::Encrypt(plaintext, dm_key_);
    if (!blob) {
        return std::unexpected(blob.error());
    }

    auto insert = db_.Exec(
        "INSERT INTO chat_dms (sender, recipient, nonce, ciphertext) VALUES (?, ?, ?, ?);",
        {sender, recipient, blob->nonce_b64, blob->ciphertext_b64});
    if (!insert) {
        return std::unexpected(insert.error());
    }

    auto prune = db_.Exec(
        "DELETE FROM chat_dms WHERE "
        "((sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?)) AND id NOT IN ("
        "  SELECT id FROM chat_dms WHERE "
        "  ((sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?)) "
        "  ORDER BY id DESC LIMIT ?"
        ");",
        {sender, recipient, recipient, sender,
         sender, recipient, recipient, sender,
         static_cast<int>(kDmHistoryLimit)});
    if (!prune) {
        return std::unexpected(prune.error());
    }
    return {};
}

Result<std::vector<ChatMessageEntry>> ChatService::GetDirectHistory(const std::string& user_a,
                                                                    const std::string& user_b) {
    auto rows = db_.Query(
        "SELECT sender, nonce, ciphertext FROM chat_dms WHERE "
        "(sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?) "
        "ORDER BY id ASC;",
        {user_a, user_b, user_b, user_a});
    if (!rows) {
        return std::unexpected(rows.error());
    }

    std::vector<ChatMessageEntry> history;
    history.reserve(rows->size());
    for (const auto& row : rows.value()) {
        Crypto::EncryptedBlob blob{row.Get<std::string>("nonce"),
                                  row.Get<std::string>("ciphertext")};
        auto plaintext = Crypto::Decrypt(blob, dm_key_);
        if (!plaintext) {
            return std::unexpected(plaintext.error());
        }
        history.push_back({row.Get<std::string>("sender"), *plaintext});
    }
    return history;
}

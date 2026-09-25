#include <common/email_send_log.hpp>
#include <logger.hpp>
#include <chrono>

namespace {
int NowSeconds() {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
}  // namespace

EmailSendLog::EmailSendLog(Database& db) : db_(db) {}

Result<int> EmailSendLog::SendsInLast24h(int user_id) {
    int since = NowSeconds() - 86400;
    auto row_result = db_.QueryOne(
        "SELECT COUNT(*) as c FROM email_send_log WHERE user_id = ? AND sent_at > ?;",
        {user_id, since});
    if (!row_result) return std::unexpected(row_result.error());
    return row_result->value().Get<int>("c");
}

void EmailSendLog::RecordSend(int user_id) {
    auto result = db_.Exec(
        "INSERT INTO email_send_log (user_id, sent_at) VALUES (?, ?);",
        {user_id, NowSeconds()});
    if (!result) {
        Logger::Error("[Email] failed to record send for user_id=" + std::to_string(user_id) +
                      ": " + result.error().message);
    }
}

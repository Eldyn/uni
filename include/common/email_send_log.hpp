#pragma once
#include <result.hpp>
#include <database.hpp>

/**
 * @file email_send_log.hpp
 * @brief Shared per-account transactional-email send accounting, backed by the
 * `email_send_log` table. One daily-cap implementation for every flow that
 * sends mail (verification, password reset, ...).
 */

/**
 * @class EmailSendLog
 * @brief Counts and records outbound mail per user over a rolling 24h window,
 * used to rate-limit resends independently of any transport.
 */
class EmailSendLog {
public:
    /**
     * @param db Database to read/write the send log against.
     */
    explicit EmailSendLog(Database& db = Database::Get());

    /**
     * @brief Counts emails sent to this user in the last 24h.
     * @param user_id Account to check.
     * @return Result<int> The count, or a DB failure.
     */
    Result<int> SendsInLast24h(int user_id);

    /**
     * @brief Records that an email was sent to this user, for the rolling cap.
     * @param user_id Account the email was sent to.
     */
    void RecordSend(int user_id);

private:
    Database& db_;
};

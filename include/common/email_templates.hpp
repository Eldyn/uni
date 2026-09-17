#pragma once
#include <common/email_sender.hpp>
#include <string>

/**
 * @file email_templates.hpp
 * @brief Localized HTML/plaintext template rendering for outbound
 * transactional email (verification code, legacy-account migration
 * nudge). Copy is drawn from the generated `email_copy::Get()` lookup
 * (see scripts/generate_email_copy_hpp.py), so backend copy never drifts
 * from the frontend's translation bundles.
 */

/**
 * @struct VerifyEmailData
 * @brief Inputs needed to render either the verification or migration
 * email for a single recipient.
 */
struct VerifyEmailData {
    std::string username;
    std::string code;
    std::string magic_link;
    std::string locale;
};

/**
 * @brief Renders the "verify your email" message: a 6-digit code plus a
 * magic link, in the given locale (falls back to English for unknown
 * locales/keys).
 */
OutboundEmail RenderVerifyEmail(const VerifyEmailData& data);

/**
 * @brief Renders the "please verify to keep full access" nudge sent to
 * pre-existing accounts migrated into the email-verification flow.
 */
OutboundEmail RenderMigrationEmail(const VerifyEmailData& data);

/**
 * @brief Builds the magic link a caller should put in
 * `VerifyEmailData::magic_link` for the given code:
 * `Env::Get("EMAIL_VERIFY_BASE_URL", "https://playuni.app") + "/profile/verify/" + code`.
 * Kept separate from the render functions above so they stay pure and
 * environment-free (easy to unit test without touching process env vars).
 */
std::string BuildVerifyMagicLink(const std::string& code);

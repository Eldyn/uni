#pragma once
#include <string>
#include <memory>
#include <result.hpp>

/**
 * @file email_sender.hpp
 * @brief Seam for outbound transactional email: a common IEmailSender
 * interface plus two implementations picked at runtime by `EMAIL_MODE`.
 */

/**
 * @struct OutboundEmail
 * @brief A fully-rendered email ready to hand to an IEmailSender.
 */
struct OutboundEmail {
    std::string to_address;
    std::string to_name;
    std::string subject;
    std::string html_body;
    std::string text_body;
};

/**
 * @class IEmailSender
 * @brief Abstraction over "how an email actually leaves the process",
 * so callers (verification flows, notifications, ...) never depend on a
 * concrete transport.
 */
class IEmailSender {
public:
    virtual ~IEmailSender() = default;

    /**
     * @brief Delivers (or, for dev senders, renders) the given email.
     * @param mail The rendered email to send.
     * @return VoidResult Empty on success, or the reason delivery failed.
     */
    virtual VoidResult Send(const OutboundEmail& mail) = 0;
};

/**
 * @class DevFileEmailSender
 * @brief Dev/CI-safe sender: writes the rendered HTML and text bodies to
 * files under a local directory instead of touching the network. Used
 * whenever `EMAIL_MODE` is unset, empty, or "dev".
 */
class DevFileEmailSender final : public IEmailSender {
public:
    /**
     * @param dir Directory the rendered `.html`/`.txt` files are written into.
     * Created (including parents) on first send if missing.
     */
    explicit DevFileEmailSender(std::string dir);

    /**
     * @brief Writes `<unix>-<to_address>.html` and `.txt` under the
     * configured directory. Never opens a socket.
     */
    VoidResult Send(const OutboundEmail& mail) override;

private:
    std::string dir_;
};

/**
 * @class BrevoEmailSender
 * @brief Live sender that POSTs to the Brevo transactional email API.
 * Declared here for the seam; implemented later.
 */
class BrevoEmailSender final : public IEmailSender {
public:
    /**
     * @param api_key Brevo API key. Never logged, never persisted anywhere
     * beyond this in-memory copy.
     */
    explicit BrevoEmailSender(std::string api_key);

    /**
     * @brief Not implemented yet.
     */
    VoidResult Send(const OutboundEmail& mail) override;

private:
    std::string api_key_;
};

/**
 * @brief Picks the concrete IEmailSender by the `EMAIL_MODE` env var.
 * "dev" (or unset/empty) -> DevFileEmailSender using `EMAIL_DEV_DIR`.
 * "live" with a non-empty `EMAIL_KEY` -> BrevoEmailSender.
 * "live" with an empty `EMAIL_KEY` -> falls back to DevFileEmailSender,
 * logging a warning (never the key itself, which is never inspected beyond
 * an emptiness check).
 * @return std::unique_ptr<IEmailSender> The selected sender.
 */
std::unique_ptr<IEmailSender> MakeEmailSender();

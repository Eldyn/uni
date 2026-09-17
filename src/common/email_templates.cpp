#include <common/email_templates.hpp>
#include <common/email_copy.hpp>
#include <common/env.hpp>

namespace {

/**
 * @brief Escapes the handful of characters that matter inside HTML text
 * nodes/attributes. Copy pulled from `email_copy::Get()` is trusted (it
 * comes from the repo's own translation bundles, escaped at codegen time),
 * but user-influenced fields never appear in these templates' body text,
 * so this exists purely for defense in depth around the one field that
 * could ever carry arbitrary characters.
 */
std::string HtmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string Copy(const std::string& key, const std::string& locale) {
    return email_copy::Get(key, locale);
}

/**
 * @brief Shared HTML skeleton for both templates: dark pixel-art palette
 * approximated with inline styles only (no `<link>`, no `<style>`, no web
 * fonts, no images — email clients can't be trusted to load any of that).
 *
 * The hard `2px 2px 0 #1a1a1a` offset shadow used across the site
 * (see frontend/src/app.css --elevation-1) isn't real box-shadow here:
 * most email clients strip or mis-render CSS box-shadow, so the "shadow"
 * is faked with a solid #1a1a1a table cell sitting behind the card,
 * exposed by giving the card a small negative margin into it.
 */
std::string BuildHtmlBody(const std::string& heading,
                           const std::string& intro,
                           const std::string& code,
                           const std::string& code_label,
                           const std::string& note,
                           const std::string& link_prompt,
                           const std::string& magic_link,
                           const std::string& footer) {
    std::string html;
    html += "<table role=\"presentation\" width=\"100%\" bgcolor=\"#16171d\" "
            "cellpadding=\"0\" cellspacing=\"0\" border=\"0\" "
            "style=\"background:#16171d;\"><tr><td align=\"center\" style=\"padding:32px 16px;\">";
    // Offset-shadow wrapper: solid #1a1a1a cell behind the card, exposed by
    // the card's negative margin below.
    html += "<table role=\"presentation\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\"><tr>"
            "<td style=\"background:#1a1a1a;\">";
    html += "<table role=\"presentation\" width=\"480\" bgcolor=\"#2a2a2d\" cellpadding=\"0\" "
            "cellspacing=\"0\" border=\"0\" "
            "style=\"border:2px solid #2e303a;margin:-2px 2px 2px -2px;background:#2a2a2d;\">";
    html += "<tr><td style=\"padding:32px;font-family:sans-serif;\">";

    html += "<h1 style=\"color:#f3f4f6;font-family:sans-serif;font-size:22px;margin:0 0 16px 0;\">";
    html += HtmlEscape(heading);
    html += "</h1>";

    html += "<p style=\"color:#9ca3af;font-family:sans-serif;font-size:15px;line-height:1.5;margin:0 0 16px 0;\">";
    html += HtmlEscape(intro);
    html += "</p>";

    if (!code_label.empty()) {
        html += "<p style=\"color:#9ca3af;font-family:sans-serif;font-size:15px;margin:0 0 8px 0;\">";
        html += HtmlEscape(code_label);
        html += "</p>";
    }

    html += "<p style=\"color:#c084fc;font-size:34px;letter-spacing:10px;font-family:monospace;"
            "text-align:center;margin:8px 0 24px 0;\">";
    html += HtmlEscape(code);
    html += "</p>";

    if (!note.empty()) {
        html += "<p style=\"color:#9ca3af;font-family:sans-serif;font-size:15px;line-height:1.5;margin:0 0 16px 0;\">";
        html += HtmlEscape(note);
        html += "</p>";
    }

    if (!link_prompt.empty()) {
        html += "<p style=\"color:#9ca3af;font-family:sans-serif;font-size:15px;line-height:1.5;margin:0 0 8px 0;\">";
        html += HtmlEscape(link_prompt);
        html += "</p>";
    }

    html += "<p style=\"text-align:center;margin:16px 0 24px 0;\">";
    html += "<a href=\"" + HtmlEscape(magic_link) + "\" "
            "style=\"background:#c084fc;color:#16171d;font-family:sans-serif;font-weight:bold;"
            "font-size:15px;text-decoration:none;padding:12px 28px;display:inline-block;\">";
    html += HtmlEscape(magic_link);
    html += "</a></p>";

    if (!footer.empty()) {
        html += "<p style=\"color:#9ca3af;font-family:sans-serif;font-size:12px;line-height:1.5;margin:24px 0 0 0;\">";
        html += HtmlEscape(footer);
        html += "</p>";
    }

    html += "</td></tr></table>";
    html += "</td></tr></table>";
    html += "</td></tr></table>";
    return html;
}

std::string BuildTextBody(const std::string& intro,
                           const std::string& code,
                           const std::string& note,
                           const std::string& magic_link) {
    std::string text;
    text += intro + "\n\n";
    text += code + "\n\n";
    text += magic_link + "\n\n";
    text += note + "\n";
    return text;
}

}  // namespace

std::string BuildVerifyMagicLink(const std::string& code) {
    return Env::Get("EMAIL_VERIFY_BASE_URL", "https://playuni.app") + "/profile/verify/" + code;
}

OutboundEmail RenderVerifyEmail(const VerifyEmailData& d) {
    const std::string& locale = d.locale;

    OutboundEmail mail;
    mail.to_name = d.username;
    mail.subject = Copy("email_verify_subject", locale);

    const std::string intro = Copy("email_verify_body_intro", locale);
    const std::string code_label = Copy("email_verify_body_code_label", locale);
    const std::string expiry = Copy("email_verify_body_expiry", locale);
    const std::string link_prompt = Copy("email_verify_body_link", locale);
    const std::string ignore = Copy("email_verify_body_ignore", locale);

    mail.html_body = BuildHtmlBody(mail.subject, intro, d.code, code_label, expiry,
                                    link_prompt, d.magic_link, ignore);
    mail.text_body = BuildTextBody(intro, d.code, expiry, d.magic_link) + "\n" + ignore + "\n";
    return mail;
}

OutboundEmail RenderMigrationEmail(const VerifyEmailData& d) {
    const std::string& locale = d.locale;

    OutboundEmail mail;
    mail.to_name = d.username;
    mail.subject = Copy("email_migration_subject", locale);

    const std::string intro = Copy("email_migration_body_intro", locale);
    const std::string code_label = Copy("email_verify_body_code_label", locale);
    const std::string deadline = Copy("email_migration_body_deadline", locale);
    const std::string link_prompt = Copy("email_verify_body_link", locale);

    mail.html_body = BuildHtmlBody(mail.subject, intro, d.code, code_label, deadline,
                                    link_prompt, d.magic_link, "");
    mail.text_body = BuildTextBody(intro, d.code, deadline, d.magic_link);
    return mail;
}

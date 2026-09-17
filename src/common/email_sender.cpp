#include <common/email_sender.hpp>
#include <common/env.hpp>
#include <logger.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace {
long long NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
}  // namespace

DevFileEmailSender::DevFileEmailSender(std::string dir) : dir_(std::move(dir)) {}

VoidResult DevFileEmailSender::Send(const OutboundEmail& mail) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) {
        return std::unexpected(Error::Internal(
            "[Email] failed to create dev output directory: " + ec.message()));
    }

    std::string stem = std::to_string(NowSeconds()) + "-" + mail.to_address;
    std::filesystem::path html_path = std::filesystem::path(dir_) / (stem + ".html");
    std::filesystem::path text_path = std::filesystem::path(dir_) / (stem + ".txt");

    std::ofstream html_file(html_path, std::ios::trunc);
    if (!html_file) {
        return std::unexpected(Error::Internal("[Email] failed to open " + html_path.string()));
    }
    html_file << mail.html_body;
    html_file.close();

    std::ofstream text_file(text_path, std::ios::trunc);
    if (!text_file) {
        return std::unexpected(Error::Internal("[Email] failed to open " + text_path.string()));
    }
    text_file << mail.text_body;
    text_file.close();

    Logger::Info("[Email] dev-mode wrote " + html_path.string());
    Logger::Info("[Email] dev-mode wrote " + text_path.string());

    return {};
}

BrevoEmailSender::BrevoEmailSender(std::string api_key) : api_key_(std::move(api_key)) {}

VoidResult BrevoEmailSender::Send(const OutboundEmail& /*mail*/) {
    // Live Brevo delivery is not wired yet. Deliberately never touches
    // api_key_ beyond storing it, and never logs it.
    return std::unexpected(Error::Internal("[Email] BrevoEmailSender::Send not implemented yet"));
}

std::unique_ptr<IEmailSender> MakeEmailSender() {
    std::string mode = Env::Get("EMAIL_MODE", "dev");

    if (mode == "live") {
        std::string key = Env::Get("EMAIL_KEY", "");
        if (!key.empty()) {
            return std::make_unique<BrevoEmailSender>(std::move(key));
        }
        // Never log the key or any substring of it, not even its length.
        Logger::Warn("[Email] EMAIL_MODE=live but EMAIL_KEY is unset, falling back to dev mode");
    }

    return std::make_unique<DevFileEmailSender>(Env::Get("EMAIL_DEV_DIR", "./email-out"));
}

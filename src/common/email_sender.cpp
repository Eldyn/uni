#include <common/email_sender.hpp>
#include <common/env.hpp>
#include <logger.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cctype>
#include <algorithm>
#include <nlohmann/json.hpp>

/**
 * @def UNI_HAS_LIBCURL
 * @brief Compile-time switch selecting whether BrevoEmailSender::Send has a
 * real libcurl-backed implementation (1) or a stub that fails without
 * touching the network (0).
 *
 * `src/common/email_sender.cpp` is compiled into both `uni_server` (which
 * links `CURL::libcurl`) and `uni_tests` (which deliberately does not, since
 * no test ever constructs a live BrevoEmailSender). CMakeLists.txt defines
 * this to 1 only for `uni_server`; it defaults to 0 here so `uni_tests`
 * never needs curl headers or the library at link time.
 */
#ifndef UNI_HAS_LIBCURL
#define UNI_HAS_LIBCURL 0
#endif

#if UNI_HAS_LIBCURL
#include <curl/curl.h>
#endif

using json = nlohmann::json;

namespace {
long long NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Filenames in EMAIL_DEV_DIR are built from an address supplied by the
// caller. No caller is wired into this task yet, but nothing here should
// trust that every future caller pre-sanitizes its input: any character
// that isn't alphanumeric, '.', '-', or '_' is replaced with '_', which
// also neutralises path separators and ".." traversal segments while
// keeping the filename human-readable in EMAIL_DEV_DIR.
std::string SanitizeForFilename(const std::string& raw) {
    std::string safe;
    safe.reserve(raw.size());
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_') {
            safe.push_back(static_cast<char>(c));
        } else {
            safe.push_back('_');
        }
    }
    return safe;
}

#if UNI_HAS_LIBCURL
// Replaces curl's default behaviour of writing the response body to stdout:
// instead it's appended to an in-memory buffer so a failure path can log a
// short, bounded snippet of it. Never touches request headers, so the
// api-key header set on the request is never within reach of this callback.

// Capped at kMaxCapturedBody (only the first 200 chars are ever logged) so a
// pathological/huge response body can't grow this buffer unbounded. Wrapped
// in try/catch: this callback is invoked directly from libcurl's C stack, and
// std::string::append can throw std::bad_alloc; letting a C++ exception
// propagate through a C library's call frames is undefined behaviour, so any
// exception here is swallowed and reported to curl as a short write, which
// aborts the transfer cleanly via CURLE_WRITE_ERROR instead.
constexpr size_t kMaxCapturedBody = 8192;

size_t AppendResponseBody(char* data, size_t size, size_t count, void* user) {
    const size_t total = size * count;
    try {
        auto* out = static_cast<std::string*>(user);
        if (out->size() < kMaxCapturedBody) {
            size_t remaining = kMaxCapturedBody - out->size();
            out->append(data, std::min(remaining, total));
        }
        return total;
    } catch (...) {
        return 0;  // Signals a short write to libcurl, aborting the transfer.
    }
}
#endif
}  // namespace

DevFileEmailSender::DevFileEmailSender(std::string dir) : dir_(std::move(dir)) {}

VoidResult DevFileEmailSender::Send(const OutboundEmail& mail) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) {
        return std::unexpected(Error::Internal(
            "[Email] failed to create dev output directory: " + ec.message()));
    }

    std::string stem = std::to_string(NowSeconds()) + "-" + SanitizeForFilename(mail.to_address);
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

VoidResult BrevoEmailSender::Send(const OutboundEmail& mail) {
#if UNI_HAS_LIBCURL
  try {
    json payload = {
        {"sender", {{"name", Env::Get("EMAIL_FROM_NAME", "")},
                    {"email", Env::Get("EMAIL_FROM_ADDRESS", "")}}},
        {"to", json::array({{{"email", mail.to_address}, {"name", mail.to_name}}})},
        {"subject", mail.subject},
        {"htmlContent", mail.html_body},
        {"textContent", mail.text_body}
    };
    // error_handler_t::replace swaps any invalid UTF-8 byte sequence in the
    // user-controlled fields above (to_address, to_name, subject, bodies)
    // for U+FFFD instead of throwing nlohmann::json::type_error (316), which
    // would otherwise escape unhandled on the EmailQueue worker thread.
    std::string body = payload.dump(-1, ' ', false, json::error_handler_t::replace);

    CURL* curl = curl_easy_init();
    if (!curl) {
        return std::unexpected(Error::Internal("[Email] failed to initialise curl handle"));
    }

    // api_key_ only ever flows into this header, which is never logged, never
    // otherwise inspected, and freed (curl_slist_free_all) right after the
    // request completes.
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "accept: application/json");
    headers = curl_slist_append(headers, "content-type: application/json");
    headers = curl_slist_append(headers, ("api-key: " + api_key_).c_str());

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.brevo.com/v3/smtp/email");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AppendResponseBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    // Prevents libcurl's non-threaded resolver from using SIGALRM-based
    // timeout handling, which can corrupt process state when curl runs off
    // the main thread (as it does here, from an EmailQueue worker thread).
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode perform_result = curl_easy_perform(curl);
    if (perform_result != CURLE_OK) {
        std::string curl_error = curl_easy_strerror(perform_result);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::unexpected(Error::Internal("[Email] Brevo request failed: " + curl_error));
    }

    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (status_code < 200 || status_code >= 300) {
        Logger::Error("[Email] Brevo returned " + std::to_string(status_code) +
                      ": " + response_body.substr(0, 200));
        return std::unexpected(Error::Internal("[Email] Brevo returned " +
                                                 std::to_string(status_code)));
    }

    return {};
  } catch (const std::exception& e) {
    // Defense in depth: nothing in the block above is expected to throw once
    // payload.dump() uses error_handler_t::replace, but this still converts
    // any unforeseen exception (e.g. std::bad_alloc) into a VoidResult
    // instead of letting it escape Send() and terminate the process on the
    // EmailQueue worker thread.
    return std::unexpected(Error::Internal(std::string("[Email] Brevo send failed: ") + e.what()));
  } catch (...) {
    return std::unexpected(Error::Internal("[Email] Brevo send failed: unknown exception"));
  }
#else
    (void)mail;
    return std::unexpected(Error::Internal(
        "[Email] BrevoEmailSender::Send unavailable: this binary was built without libcurl"));
#endif
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

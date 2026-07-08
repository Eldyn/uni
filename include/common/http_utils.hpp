#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/**
 * @file http_utils.hpp
 * @brief Pure, uWS-free helpers backing the static-file-serving side of the HTTP layer.
 *
 * Kept free of any uWebSockets type so they can be exercised directly from unit
 * tests without a live socket or SSL app.
 */

namespace http {

/**
 * @brief Deduces the MIME type of a file from its extension.
 * @param path The file path (or bare extension) to inspect.
 * @return std::string The MIME Type (e.g. "text/html"), or
 *         "application/octet-stream" if the extension is unrecognised.
 * @tag CMN-HTTP-MTH-005
 */
std::string GetMimeType(std::string_view path);

/**
 * @brief Resolves a request path against a served root, guarding against
 *        path traversal.
 *
 * Canonicalises both @p root and `root / request_path`, then confirms the
 * resolved file still lives inside @p root (i.e. its path relative to @p root
 * does not start with ".."). This stops a request such as "/../../etc/passwd"
 * from escaping the served directory.
 *
 * @param root The canonical directory being served.
 * @param request_path The path portion of the incoming request, relative to @p root.
 * @return std::optional<std::filesystem::path> The canonical, in-root file path,
 *         or std::nullopt if canonicalisation failed or the result escapes @p root.
 * @tag CMN-HTTP-MTH-006
 */
std::optional<std::filesystem::path> ResolveSafePath(const std::filesystem::path& root,
                                                       std::string_view request_path);

/**
 * @brief Selects the Cache-Control policy for a static asset, keyed on its
 *        path within the served root.
 *
 *   *.html              -> "no-cache": always revalidate. index.html is the
 *                          entry point that names the content-hashed bundles,
 *                          so a returning client must re-check it or a redeploy
 *                          would stay invisible. "no-cache" still allows storing
 *                          the copy; paired with the ETag below the recheck is a
 *                          cheap 304 when nothing changed.
 *   assets/*            -> immutable for a year. Vite content-hashes these
 *                          (e.g. assets/index-8FzcvdAa.js); a new build yields a
 *                          new name, so the old URL can be trusted forever.
 *   fonts/*             -> 30 days. Stable filenames whose bytes effectively
 *                          never change; long TTL avoids re-fetching the ~1 MB
 *                          JetBrainsMono on every visit, ETag covers the rare edit.
 *   everything else     -> 1 day (favicon, icons, root images).
 *
 * @param relative_path The asset's path relative to the served root.
 * @return std::string The Cache-Control header value to send.
 * @tag CMN-HTTP-MTH-007
 */
std::string CacheControlFor(std::string_view relative_path);

/**
 * @brief Derives a weak ETag from a file's size and last-write time.
 *
 * An opaque validator (RFC 7232) that only has to change when the file does —
 * size+mtime captures that without hashing the contents.
 *
 * @param file The file to stat.
 * @return std::string The ETag value, or an empty string if the file could
 *         not be stat'd (in which case the caller should omit the header).
 * @tag CMN-HTTP-MTH-008
 */
std::string MakeETag(const std::filesystem::path& file);

}  // namespace http

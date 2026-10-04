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
 */
std::string GetMimeType(std::string_view path);

/**
 * @brief Tells whether an unresolved request path is a client-side route
 *        that should fall back to index.html, rather than a missing asset
 *        that should 404.
 *
 * A client-side route has no dot in its final path segment (e.g. "browse",
 * "profile/stats"); a real asset request always does ("index-abc.js",
 * "favicon.ico"). This only needs to be checked when the literal file was
 * NOT found on disk — an existing file is always served as itself,
 * regardless of what this function would say about its path.
 *
 * @param relative_path The request path, without the served root, that
 *        failed to resolve to a real file.
 * @return bool True if the caller should serve index.html instead of 404.
 */
bool IsClientRoute(std::string_view relative_path);

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
 */
std::string CacheControlFor(std::string_view relative_path);

/**
 * @brief Tells whether a client's Accept-Encoding header welcomes a coding.
 *
 * Deliberately lenient: any mention of the coding counts, except an explicit
 * zero qvalue (e.g. "br;q=0"), which RFC 9110 defines as a refusal.
 *
 * @param accept_encoding The raw Accept-Encoding header value (may be empty).
 * @param coding The content-coding token to look for, e.g. "gzip" or "br".
 * @return bool True if a body in that coding may be sent.
 */
bool AcceptsEncoding(std::string_view accept_encoding, std::string_view coding);

/** @brief Shorthand for AcceptsEncoding(accept_encoding, "gzip"). */
bool AcceptsGzip(std::string_view accept_encoding);

/**
 * @brief Locates a build-time compressed sidecar of a static asset, if one exists.
 *
 * The frontend build writes a "<file>.br" and a "<file>.gz" next to every
 * compressible asset (see frontend/scripts/gzip-assets.js), so the server can
 * ship a compressed body without spending CPU per request. The JS bundle
 * carrying three.js is what makes this worth it: it compresses to a fraction
 * of its size.
 *
 * @param file The resolved, in-root path of the asset actually requested.
 * @param suffix The sidecar suffix, ".gz" by default.
 * @return std::optional<std::filesystem::path> The sidecar's path, or
 *         std::nullopt when none exists or @p file is itself a sidecar.
 */
std::optional<std::filesystem::path> PrecompressedVariant(const std::filesystem::path& file,
                                                          std::string_view suffix = ".gz");

/** @brief A compressed sidecar chosen for a response, and its Content-Encoding. */
struct PrecompressedBody {
    std::filesystem::path path;
    std::string_view      encoding;
};

/**
 * @brief Picks the best sidecar the client accepts: Brotli first, then gzip.
 *
 * @param file The resolved, in-root path of the asset actually requested.
 * @param accept_encoding The raw Accept-Encoding header value (may be empty).
 * @return std::optional<PrecompressedBody> The sidecar to send, or
 *         std::nullopt when the identity body should be served.
 */
std::optional<PrecompressedBody> SelectPrecompressed(const std::filesystem::path& file,
                                                     std::string_view accept_encoding);

/**
 * @brief Derives a weak ETag from a file's size and last-write time.
 *
 * An opaque validator (RFC 7232) that only has to change when the file does:
 * size+mtime captures that without hashing the contents.
 *
 * @param file The file to stat.
 * @return std::string The ETag value, or an empty string if the file could
 *         not be stat'd (in which case the caller should omit the header).
 */
std::string MakeETag(const std::filesystem::path& file);

/**
 * @brief Whether a WebSocket upgrade from this Origin is acceptable.
 *
 * An empty Origin (non-browser client: curl, native, tests) is allowed — such
 * a client cannot be CSRF'd. A non-empty Origin must either appear verbatim in
 * the comma-separated @p allowlist, or (when the allowlist is empty) share the
 * request's Host, i.e. be same-origin. This is the CSWSH defence for the
 * SameSite=None ws_token cookie.
 *
 * @param origin The request's Origin header (may be empty).
 * @param host The request's Host header, used for the same-origin fallback.
 * @param allowlist Comma-separated allowed origins; empty selects same-origin.
 * @return bool True if the upgrade may proceed.
 */
bool IsAllowedWsOrigin(std::string_view origin, std::string_view host,
                       std::string_view allowlist);

/**
 * @brief Whether a request path contains a dot-leading segment that must not
 *        be served.
 *
 * Dotfiles and dot-directories (.env, .git/config, .ssh) are never intended
 * to be web-reachable; refusing them outright means a stray file dropped into
 * the served root cannot leak its contents. The sole exemption is the literal
 * ".well-known/" prefix, which atproto and other standard discovery endpoints
 * legitimately live under.
 *
 * @param relative_path The request path, without the served root.
 * @return bool True if any path segment other than the allowed .well-known
 *         prefix starts with '.'.
 */
bool HasForbiddenDotSegment(std::string_view relative_path);

}  // namespace http

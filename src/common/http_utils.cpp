#include <common/http_utils.hpp>
#include <common/http.hpp>

namespace fs = std::filesystem;

namespace http {

std::string GetMimeType(std::string_view path) {
    if (path.ends_with(".html"))   return "text/html";
    if (path.ends_with(".js"))     return "text/javascript";
    if (path.ends_with(".css"))    return "text/css";
    if (path.ends_with(".svg"))    return "image/svg+xml";
    if (path.ends_with(".png"))    return "image/png";
    if (path.ends_with(".woff2"))  return "font/woff2";
    if (path.ends_with(".woff"))   return "font/woff";
    if (path.ends_with(".xml"))    return "application/xml";
    // INFO: three.js instantiates the draco/basis decoders straight from the
    //       response, which only works when the media type is exact.
    if (path.ends_with(".wasm"))   return "application/wasm";
    if (path.ends_with(".json"))   return "application/json";
    if (path.ends_with(".webmanifest")) return "application/manifest+json";
    return "application/octet-stream";
}

bool IsClientRoute(std::string_view relative_path) {
    if (relative_path.empty()) return false;

    const auto last_slash = relative_path.find_last_of('/');
    const std::string_view final_segment =
        last_slash == std::string_view::npos ? relative_path : relative_path.substr(last_slash + 1);

    return final_segment.find('.') == std::string_view::npos;
}

std::optional<fs::path> ResolveSafePath(const fs::path& root, std::string_view request_path) {
    std::error_code ec;
    fs::path canonical_root = fs::weakly_canonical(root, ec);
    fs::path file_path      = fs::weakly_canonical(canonical_root / request_path, ec);
    fs::path rel            = file_path.lexically_relative(canonical_root);
    const bool within_root  = !ec && !rel.empty() && *rel.begin() != "..";

    if (!within_root) return std::nullopt;
    return file_path;
}

std::string CacheControlFor(std::string_view relative_path) {
    if (relative_path.ends_with(".html"))   return "no-cache";
    if (relative_path.starts_with("assets/")) return "public, max-age=31536000, immutable";
    if (relative_path.starts_with("fonts/"))  return "public, max-age=2592000";
    return "public, max-age=86400";
}

bool AcceptsEncoding(std::string_view accept_encoding, std::string_view coding) {
    const size_t at = accept_encoding.find(coding);
    if (at == std::string_view::npos) return false;

    // INFO: A bare coding (or one followed by anything other than a qvalue)
    //       is an acceptance. Only an explicit q=0 is a refusal, per RFC 9110.
    std::string_view params = accept_encoding.substr(at + coding.size());
    const size_t equals     = params.find('=');
    if (!params.starts_with(";") || equals == std::string_view::npos) return true;

    for (const char c : params.substr(equals + 1)) {
        if (c == ',' || c == ' ') break;
        if (c != '0' && c != '.') return true;
    }
    return false;
}

bool AcceptsGzip(std::string_view accept_encoding) {
    return AcceptsEncoding(accept_encoding, "gzip");
}

std::optional<fs::path> PrecompressedVariant(const fs::path& file, std::string_view suffix) {
    // A direct request for a sidecar itself is just a normal file download,
    // never a compressed response for some other resource.
    if (file.extension() == ".gz" || file.extension() == ".br") return std::nullopt;

    fs::path sidecar = file;
    sidecar += suffix;

    std::error_code ec;
    if (!fs::exists(sidecar, ec) || ec) return std::nullopt;
    return sidecar;
}

std::optional<PrecompressedBody> SelectPrecompressed(const fs::path& file,
                                                     std::string_view accept_encoding) {
    if (AcceptsEncoding(accept_encoding, "br")) {
        if (auto sidecar = PrecompressedVariant(file, ".br")) {
            return PrecompressedBody{*sidecar, "br"};
        }
    }
    if (AcceptsGzip(accept_encoding)) {
        if (auto sidecar = PrecompressedVariant(file, ".gz")) {
            return PrecompressedBody{*sidecar, "gzip"};
        }
    }
    return std::nullopt;
}

namespace {

std::optional<std::size_t> ParseDecimal(std::string_view digits) {
    if (digits.empty() || digits.size() > 18) return std::nullopt;
    std::size_t value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    return value;
}

}  // namespace

ByteRange ParseByteRange(std::string_view header, std::size_t total) {
    constexpr std::string_view kUnit = "bytes=";
    if (!header.starts_with(kUnit)) return {};

    std::string_view spec = header.substr(kUnit.size());
    if (spec.find(',') != std::string_view::npos) return {};

    const std::size_t dash = spec.find('-');
    if (dash == std::string_view::npos) return {};

    const std::string_view first = spec.substr(0, dash);
    const std::string_view last  = spec.substr(dash + 1);

    std::size_t start = 0;
    std::size_t end   = 0;  // inclusive

    if (first.empty()) {
        // Suffix form "-n": the final n bytes.
        const auto suffix = ParseDecimal(last);
        if (!suffix) return {};
        if (*suffix == 0 || total == 0) return {ByteRange::Kind::kUnsatisfiable, 0, 0};
        start = total > *suffix ? total - *suffix : 0;
        end   = total - 1;
    } else {
        const auto parsed_start = ParseDecimal(first);
        if (!parsed_start) return {};
        start = *parsed_start;

        if (last.empty()) {
            end = total == 0 ? 0 : total - 1;
        } else {
            const auto parsed_end = ParseDecimal(last);
            if (!parsed_end || *parsed_end < start) return {};
            end = *parsed_end;
        }
        if (start >= total) return {ByteRange::Kind::kUnsatisfiable, 0, 0};
        if (end >= total) end = total - 1;
    }

    return {ByteRange::Kind::kPartial, start, end - start + 1};
}

std::string MakeETag(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) return "";
    const auto mtime = fs::last_write_time(file, ec);
    if (ec) return "";
    return "W/\"" + std::to_string(size) + "-" +
           std::to_string(mtime.time_since_epoch().count()) + "\"";
}

bool IsAllowedWsOrigin(std::string_view origin, std::string_view host,
                       std::string_view allowlist) {
    if (origin.empty()) return true;

    if (!allowlist.empty()) {
        std::size_t start = 0;
        while (start <= allowlist.size()) {
            const std::size_t comma = allowlist.find(',', start);
            std::string_view item = TrimWhitespace(
                allowlist.substr(start, comma == std::string_view::npos
                                            ? std::string_view::npos
                                            : comma - start));
            if (!item.empty() && item == origin) return true;
            if (comma == std::string_view::npos) break;
            start = comma + 1;
        }
        return false;
    }

    const std::size_t scheme = origin.find("://");
    if (scheme == std::string_view::npos) return false;
    std::string_view origin_host = origin.substr(scheme + 3);
    const std::size_t slash = origin_host.find('/');
    if (slash != std::string_view::npos) origin_host = origin_host.substr(0, slash);
    return origin_host == host;
}

bool HasForbiddenDotSegment(std::string_view relative_path) {
    std::size_t start = 0;
    while (start <= relative_path.size()) {
        const auto slash = relative_path.find('/', start);
        std::string_view seg = relative_path.substr(
            start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        // INFO: The .well-known/ discovery prefix is exempt only as the leading
        //       segment; every other segment, and any "."/".." segment anywhere,
        //       is refused so canonicalisation cannot collapse a dotfile back in.
        const bool well_known = start == 0 && seg == ".well-known" &&
                                slash != std::string_view::npos;
        if (!well_known) {
            if (seg == ".." || seg == ".") return true;
            if (!seg.empty() && seg.front() == '.') return true;
        }
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return false;
}

}  // namespace http

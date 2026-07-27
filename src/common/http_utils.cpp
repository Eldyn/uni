#include <common/http_utils.hpp>

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

bool AcceptsGzip(std::string_view accept_encoding) {
    const size_t at = accept_encoding.find("gzip");
    if (at == std::string_view::npos) return false;

    // INFO: A bare "gzip" (or one followed by anything other than a qvalue)
    //       is an acceptance. Only an explicit q=0 is a refusal, per RFC 9110.
    std::string_view params = accept_encoding.substr(at + 4);
    const size_t equals     = params.find('=');
    if (!params.starts_with(";") || equals == std::string_view::npos) return true;

    for (const char c : params.substr(equals + 1)) {
        if (c == ',' || c == ' ') break;
        if (c != '0' && c != '.') return true;
    }
    return false;
}

std::optional<fs::path> PrecompressedVariant(const fs::path& file) {
    // A direct request for the sidecar itself is just a normal file download,
    // never a gzip-encoded response for some other resource.
    if (file.extension() == ".gz") return std::nullopt;

    fs::path sidecar = file;
    sidecar += ".gz";

    std::error_code ec;
    if (!fs::exists(sidecar, ec) || ec) return std::nullopt;
    return sidecar;
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

}  // namespace http

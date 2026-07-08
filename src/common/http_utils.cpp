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

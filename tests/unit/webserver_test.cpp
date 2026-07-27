#include <doctest/doctest.h>
#include <common/http_utils.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Fixture: unique scratch directory under the system temp dir, cleaned up
// after each test case that touches the filesystem.
// ---------------------------------------------------------------------------
struct ScratchDir {
    fs::path root;

    ScratchDir() {
        root = fs::temp_directory_path() /
               ("webserver_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::remove_all(root);
        fs::create_directories(root);
    }

    ~ScratchDir() { fs::remove_all(root); }

    fs::path WriteFile(const std::string& relative, const std::string& contents = "x") const {
        fs::path full = root / relative;
        fs::create_directories(full.parent_path());
        std::ofstream(full) << contents;
        return full;
    }
};

TEST_SUITE("http_utils::GetMimeType") {

TEST_CASE("maps known extensions to their exact MIME strings") {
    CHECK(http::GetMimeType("index.html") == "text/html");
    CHECK(http::GetMimeType("bundle.js") == "text/javascript");
    CHECK(http::GetMimeType("style.css") == "text/css");
    CHECK(http::GetMimeType("icon.svg") == "image/svg+xml");
    CHECK(http::GetMimeType("logo.png") == "image/png");
    CHECK(http::GetMimeType("font.woff2") == "font/woff2");
    CHECK(http::GetMimeType("font.woff") == "font/woff");
    CHECK(http::GetMimeType("manifest.xml") == "application/xml");
    CHECK(http::GetMimeType("draco_decoder.wasm") == "application/wasm");
    CHECK(http::GetMimeType("data.json") == "application/json");
    CHECK(http::GetMimeType("app.webmanifest") == "application/manifest+json");
}

TEST_CASE("falls back to application/octet-stream for unknown extensions") {
    CHECK(http::GetMimeType("archive.zip") == "application/octet-stream");
}

TEST_CASE("falls back to application/octet-stream when there is no extension") {
    CHECK(http::GetMimeType("README") == "application/octet-stream");
}

} // TEST_SUITE

TEST_SUITE("http_utils::ResolveSafePath") {

TEST_CASE("resolves a normal in-root relative path") {
    ScratchDir dir;
    dir.WriteFile("index.html");

    auto resolved = http::ResolveSafePath(dir.root, "index.html");
    REQUIRE(resolved.has_value());
    CHECK(*resolved == fs::weakly_canonical(dir.root / "index.html"));
}

TEST_CASE("resolves a nested in-root relative path") {
    ScratchDir dir;
    dir.WriteFile("assets/foo.js");

    auto resolved = http::ResolveSafePath(dir.root, "assets/foo.js");
    REQUIRE(resolved.has_value());
    CHECK(*resolved == fs::weakly_canonical(dir.root / "assets/foo.js"));
}

TEST_CASE("rejects a straightforward traversal attempt") {
    ScratchDir dir;
    CHECK(http::ResolveSafePath(dir.root, "../../etc/passwd") == std::nullopt);
    CHECK(http::ResolveSafePath(dir.root, "../outside.txt") == std::nullopt);
}

TEST_CASE("rejects traversal mixed with legitimate leading segments") {
    ScratchDir dir;
    CHECK(http::ResolveSafePath(dir.root, "assets/../../secret") == std::nullopt);
}

} // TEST_SUITE

TEST_SUITE("http_utils::CacheControlFor") {

TEST_CASE("html paths get no-cache") {
    CHECK(http::CacheControlFor("index.html") == "no-cache");
}

TEST_CASE("assets/* paths get a year-long immutable policy") {
    CHECK(http::CacheControlFor("assets/index-8FzcvdAa.js") ==
          "public, max-age=31536000, immutable");
}

TEST_CASE("fonts/* paths get a 30-day policy") {
    CHECK(http::CacheControlFor("fonts/JetBrainsMono.woff2") == "public, max-age=2592000");
}

TEST_CASE("everything else falls back to the 1-day default") {
    CHECK(http::CacheControlFor("favicon.ico") == "public, max-age=86400");
}

} // TEST_SUITE

TEST_SUITE("http_utils::AcceptsGzip") {

TEST_CASE("accepts the encodings browsers actually send") {
    CHECK(http::AcceptsGzip("gzip"));
    CHECK(http::AcceptsGzip("gzip, deflate, br"));
    CHECK(http::AcceptsGzip("br, gzip"));
    CHECK(http::AcceptsGzip("deflate, gzip;q=1.0, *;q=0.5"));
}

TEST_CASE("refuses when gzip is absent") {
    CHECK(!http::AcceptsGzip(""));
    CHECK(!http::AcceptsGzip("deflate"));
    CHECK(!http::AcceptsGzip("br, deflate"));
}

TEST_CASE("honours an explicit zero qvalue as a refusal") {
    CHECK(!http::AcceptsGzip("gzip;q=0"));
    CHECK(!http::AcceptsGzip("gzip;q=0.0"));
    CHECK(!http::AcceptsGzip("gzip;q=0.000, deflate"));
}

TEST_CASE("treats any non-zero qvalue as acceptance") {
    CHECK(http::AcceptsGzip("gzip;q=0.1"));
    CHECK(http::AcceptsGzip("gzip;q=0.5, deflate"));
}

} // TEST_SUITE

TEST_SUITE("http_utils::PrecompressedVariant") {

TEST_CASE("finds the sidecar sitting next to the asset") {
    ScratchDir dir;
    fs::path asset = dir.WriteFile("assets/bundle.js", "console.log(1)");
    dir.WriteFile("assets/bundle.js.gz", "compressed");

    auto sidecar = http::PrecompressedVariant(asset);
    REQUIRE(sidecar.has_value());
    CHECK(sidecar->filename() == "bundle.js.gz");
}

TEST_CASE("returns nothing when no sidecar was built") {
    ScratchDir dir;
    fs::path asset = dir.WriteFile("assets/bundle.js", "console.log(1)");
    CHECK(http::PrecompressedVariant(asset) == std::nullopt);
}

TEST_CASE("never wraps a .gz request in another layer of gzip") {
    ScratchDir dir;
    fs::path sidecar = dir.WriteFile("assets/bundle.js.gz", "compressed");
    dir.WriteFile("assets/bundle.js.gz.gz", "double");

    CHECK(http::PrecompressedVariant(sidecar) == std::nullopt);
}

} // TEST_SUITE

TEST_SUITE("http_utils::MakeETag") {

TEST_CASE("produces a non-empty weak validator for an existing file") {
    ScratchDir dir;
    fs::path file = dir.WriteFile("index.html", "hello");

    std::string etag = http::MakeETag(file);
    CHECK(!etag.empty());
    CHECK(etag.starts_with("W/\""));
    CHECK(etag.ends_with("\""));
}

TEST_CASE("is deterministic for an unchanged file") {
    ScratchDir dir;
    fs::path file = dir.WriteFile("index.html", "hello");

    std::string first  = http::MakeETag(file);
    std::string second = http::MakeETag(file);
    CHECK(first == second);
}

TEST_CASE("returns an empty string when the file does not exist") {
    ScratchDir dir;
    CHECK(http::MakeETag(dir.root / "missing.html") == "");
}

} // TEST_SUITE

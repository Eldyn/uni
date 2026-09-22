#include <doctest/doctest.h>
#include <match/modload/asset_index.hpp>
#include <match/modload/mod_loader.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace match::modload;

namespace {

/** INFO: scratch mods root, removed by the destructor. */
struct TempRoot {
    fs::path root;

    TempRoot() {
        static int counter = 0;
        root = fs::temp_directory_path()
             / ("uni_asset_index_test_" + std::to_string(::getpid()) + "_"
                + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root);
    }

    ~TempRoot() { fs::remove_all(root); }

    void Write(const std::string& folder,
               const std::string& name,
               const std::string& contents) const {
        fs::path file = root / folder / name;
        fs::create_directories(file.parent_path());
        std::ofstream out(file);
        out << contents;
    }
};

void WriteImageMod(TempRoot& tmp, const std::string& bundle) {
    tmp.Write("artmod", "mod.json",
              R"({ "id": "artmod", "name": "A", "version": "1.0.0", "api": "1" })");
    tmp.Write("artmod", "cards/bomb.json",
              R"({ "id": "bomb", "face": { "kind": "image", "art": "bomb" } })");
    tmp.Write("artmod", "assets/bomb/index.json", bundle);
}

}  // namespace

TEST_CASE("asset index: resolves a variant by id tuple and hash") {
    TempRoot tmp;
    WriteImageMod(tmp,
                  R"({ "id": "bomb", "card": "bomb",
                       "slots": { "art": [ { "tier": "high", "file": "art.png" } ] } })");
    tmp.Write("artmod", "assets/bomb/art.png", "PNGDATA");

    LoadResult loaded = ScanModsDirectory(tmp.root.string());
    REQUIRE(loaded.ok());
    AssetIndex index = AssetIndex::Build(loaded);
    REQUIRE(index.entries().size() == 1);

    const std::string hash = ShortContentHash("PNGDATA");
    CHECK(index.entries()[0].hash == hash);
    CHECK(index.entries()[0].content_type == "image/png");
    CHECK(index.entries()[0].tier == AssetTier::kHigh);

    const AssetEntry* entry = index.Resolve("artmod", "bomb", "art", "high", hash);
    REQUIRE(entry != nullptr);
    CHECK(entry->slot == "art");
    CHECK(entry->size == 7);

    // Unknown tier, stale hash and traversal-shaped ids all miss.
    CHECK(index.Resolve("artmod", "bomb", "art", "low", hash) == nullptr);
    CHECK(index.Resolve("artmod", "bomb", "art", "high", "deadbeef") == nullptr);
    CHECK(index.Resolve("../etc", "bomb", "art", "high", hash) == nullptr);
    CHECK(index.Resolve("artmod", "../bomb", "art", "high", hash) == nullptr);
    CHECK(index.Resolve("artmod", "bomb", "../art", "high", hash) == nullptr);

    CHECK(AssetIndex::Url("artmod", "bomb", "art", AssetTier::kHigh, hash)
          == "/assets/artmod/bomb/art/high/" + hash);
}

TEST_CASE("asset index: variants are ordered richest tier first") {
    TempRoot tmp;
    WriteImageMod(tmp,
                  R"({ "id": "bomb", "card": "bomb",
                       "slots": { "art": [
                         { "tier": "low", "file": "art.png" },
                         { "tier": "high", "file": "art@2x.png" } ] } })");
    tmp.Write("artmod", "assets/bomb/art.png", "LOW");
    tmp.Write("artmod", "assets/bomb/art@2x.png", "HIGH");

    LoadResult loaded = ScanModsDirectory(tmp.root.string());
    REQUIRE(loaded.ok());
    AssetIndex index = AssetIndex::Build(loaded);

    std::vector<const AssetEntry*> variants =
        index.Variants("artmod", "bomb", "art");
    REQUIRE(variants.size() == 2);
    CHECK(variants[0]->tier == AssetTier::kHigh);
    CHECK(variants[1]->tier == AssetTier::kLow);
    CHECK(variants[0]->hash != variants[1]->hash);
}

TEST_CASE("asset index: content types follow the allowlist") {
    CHECK(AssetContentType("a.png") == "image/png");
    CHECK(AssetContentType("a.webp") == "image/webp");
    CHECK(AssetContentType("a.jpg") == "image/jpeg");
    CHECK(AssetContentType("a.gif") == "image/gif");
    CHECK(AssetContentType("a.json") == "application/json");
    CHECK(AssetContentType("a.bmp").empty());
    CHECK(AssetContentType("a").empty());
}

TEST_CASE("asset index: hashes are stable and short") {
    CHECK(ShortContentHash("PNGDATA") == ShortContentHash("PNGDATA"));
    CHECK(ShortContentHash("a") != ShortContentHash("b"));
    CHECK(ShortContentHash("PNGDATA").size() == 16);
}

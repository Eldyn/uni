#include <doctest/doctest.h>

#include <match/modload/mod_loader.hpp>
#include <match/modload/semantic_validator.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <unistd.h>

/**
 * @file fuzz_loader_test.cpp
 * @brief loader/validator fuzz.
 *
 * Generates random mod folders (random JSON shapes plus occasional raw bytes)
 * and asserts that `ScanModsDirectory` never throws and always returns either a
 * successful load or a non-empty structured error list. On a successful load
 * the `SemanticValidator` runs over the model and must likewise return
 * success or structured errors, never crash.
 *
 * Iterations come from `UNI_FUZZ_ITERS` (default 500; nightly CI sets 10000).
 * The PRNG is seeded fixed so every run is deterministic.
 */

namespace fs = std::filesystem;
using namespace match::modload;
using nlohmann::json;

namespace {

/** INFO: scratch directory under the system temp root, removed by the dtor. */
struct TempModsRoot {
    fs::path root;

    TempModsRoot() {
        static int counter = 0;
        root = fs::temp_directory_path()
             / ("uni_fuzz_loader_" + std::to_string(::getpid()) + "_"
                + std::to_string(counter++));
        fs::remove_all(root);
        fs::create_directories(root);
    }

    ~TempModsRoot() { fs::remove_all(root); }

    fs::path Write(const std::string& folder, const std::string& name,
                   const std::string& contents) const {
        fs::path dir = root / folder;
        fs::create_directories(dir);
        fs::path file = dir / name;
        if (file.has_parent_path()) fs::create_directories(file.parent_path());
        std::ofstream out(file);
        out << contents;
        out.close();
        return file;
    }

    void Reset() const {
        fs::remove_all(root);
        fs::create_directories(root);
    }
};

/* INFO: locate contract/schemas by walking up from this test file so the
 *       suite works regardless of the ctest working directory. */
std::string SchemaDir() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        fs::path cand = p / "contract" / "schemas";
        if (fs::is_directory(cand, ec)) return cand.string();
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

int FuzzIters() {
    const char* raw = std::getenv("UNI_FUZZ_ITERS");
    if (raw == nullptr) return 500;
    char* end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    if (end == raw || parsed <= 0) return 500;
    return static_cast<int>(parsed);
}

/** INFO: dump with a replacing error handler so invalid UTF never throws. */
std::string DumpJson(const json& value) {
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string RandomKey(std::mt19937_64& rng) {
    static const char* const kKeys[] = {
        "id",     "name",     "version",          "api",
        "cards",  "rules",    "statuses",         "mutations",
        "decks",  "nodes",    "op",               "args",
        "next",   "face",     "kind",             "color",
        "label",  "tags",     "window",           "when_played",
        "responders", "respond_with", "duration", "on_play",
    };
    if ((rng() % 3) != 0) {
        return kKeys[rng() % (sizeof(kKeys) / sizeof(kKeys[0]))];
    }
    std::string out;
    const int n = static_cast<int>(rng() % 8) + 1;
    for (int i = 0; i < n; ++i) {
        out.push_back(static_cast<char>('a' + (rng() % 26)));
    }
    return out;
}

json RandomScalar(std::mt19937_64& rng) {
    static const char* const kStrings[] = {
        "", "null", "true", "0", "-1", "1.5", "id", "name", "api",
        "version", "mod", "cards", "rules", "decks", "nodes", "op",
        "args", "next", "face", "blank", "text", "red", "\xc3\x28",
        "\xf0\x9f\x94\xa5",
    };
    switch (rng() % 5) {
        case 0:
            return nullptr;
        case 1:
            return (rng() % 2) == 0;
        case 2:
            return static_cast<int64_t>(rng() % 200) - 100;
        case 3:
            return static_cast<double>(rng() % 1000) / 10.0;
        default: {
            const std::size_t n =
                sizeof(kStrings) / sizeof(kStrings[0]);
            return std::string(kStrings[rng() % n]);
        }
    }
}

json RandomValue(std::mt19937_64& rng, int depth) {
    if (depth <= 0) return RandomScalar(rng);
    switch (rng() % 3) {
        case 0:
            return RandomScalar(rng);
        case 1: {
            json arr = json::array();
            const int n = static_cast<int>(rng() % 5);
            for (int i = 0; i < n; ++i) {
                arr.push_back(RandomValue(rng, depth - 1));
            }
            return arr;
        }
        default: {
            json obj = json::object();
            const int n = static_cast<int>(rng() % 5);
            for (int i = 0; i < n; ++i) {
                obj[RandomKey(rng)] = RandomValue(rng, depth - 1);
            }
            return obj;
        }
    }
}

std::string RandomBytes(std::mt19937_64& rng, int max_len) {
    const int n = static_cast<int>(rng() % max_len);
    std::string out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        out.push_back(static_cast<char>(rng() % 256));
    }
    return out;
}

std::string RandomJsonOrBytes(std::mt19937_64& rng, int depth) {
    if ((rng() % 5) == 0) return RandomBytes(rng, 64);
    return DumpJson(RandomValue(rng, depth));
}

json RandomManifest(std::mt19937_64& rng, const std::string& folder) {
    // INFO: half the iterations use a plausibly valid manifest so the load and
    //       semantic-validation success paths are actually exercised.
    if ((rng() % 2) == 0) {
        json manifest = {{"id", folder},
                         {"name", "Fuzz"},
                         {"version", "1.0.0"},
                         {"api", "1"}};
        return manifest;
    }
    json value = RandomValue(rng, 2);
    if (!value.is_object()) value = json::object({{"value", value}});
    return value;
}

std::string RandomCard(std::mt19937_64& rng) {
    switch (rng() % 4) {
        case 0:
            return DumpJson(json{{"id", "card_0"},
                                 {"face", {{"kind", "blank"}}}});
        case 1:
            return DumpJson(json{{"id", "card_0"},
                                 {"face", {{"kind", "image"},
                                           {"art", "bundle"}}}});
        default:
            return RandomJsonOrBytes(rng, 3);
    }
}

std::string RandomRule(std::mt19937_64& rng) {
    switch (rng() % 3) {
        case 0:
            return DumpJson(json{{"id", "rule_0"}, {"hooks", json::array()}});
        default:
            return RandomJsonOrBytes(rng, 3);
    }
}

std::string RandomDeck(std::mt19937_64& rng, const std::string& folder) {
    switch (rng() % 3) {
        case 0:
            return DumpJson(json{{"id", "classic"},
                                 {"name", "Classic"},
                                 {"namespace", folder},
                                 {"mods", json::array({folder})},
                                 {"cards", json::object()}});
        default:
            return RandomJsonOrBytes(rng, 3);
    }
}

void WriteIteration(TempModsRoot& tmp, std::mt19937_64& rng) {
    tmp.Reset();
    const std::string folder = "fuzzmod";

    if ((rng() % 10) == 0) {
        tmp.Write(folder, "mod.json", RandomBytes(rng, 80));
    } else {
        tmp.Write(folder, "mod.json", DumpJson(RandomManifest(rng, folder)));
    }
    if ((rng() % 2) == 0) tmp.Write(folder, "cards/0.json", RandomCard(rng));
    if ((rng() % 2) == 0) tmp.Write(folder, "rules/0.json", RandomRule(rng));
    if ((rng() % 2) == 0) tmp.Write(folder, "statuses/0.json", RandomRule(rng));
    if ((rng() % 2) == 0) {
        tmp.Write(folder, "decks/classic.json", RandomDeck(rng, folder));
    }
}

}  // namespace

TEST_CASE("loader fuzz: random mod folders never crash the loader") {
    const int iters = FuzzIters();
    MESSAGE("loader fuzz iterations: " << iters);

    const std::string schema_dir = SchemaDir();
    REQUIRE_FALSE(schema_dir.empty());
    SemanticValidator validator(schema_dir);

    TempModsRoot tmp;
    std::mt19937_64 rng(0x0DDB1A5E5BAD5EEDull);

    for (int i = 0; i < iters; ++i) {
        WriteIteration(tmp, rng);

        LoadResult result;
        bool load_threw = false;
        try {
            result = ScanModsDirectory(tmp.root.string());
        } catch (...) {
            load_threw = true;
        }
        CAPTURE(i);
        CHECK_FALSE(load_threw);
        if (load_threw) continue;

        if (result.ok()) {
            CHECK_FALSE(result.mods.empty());
            for (const LoadedMod& mod : result.mods) {
                std::vector<LoadError> errors;
                bool validate_threw = false;
                try {
                    errors = validator.ValidateMod(mod);
                } catch (...) {
                    validate_threw = true;
                }
                CHECK_FALSE(validate_threw);
                for (const LoadError& error : errors) {
                    CHECK_FALSE(error.check.empty());
                }
            }
        } else {
            CHECK_FALSE(result.errors.empty());
            CHECK(result.mods.empty());
            for (const LoadError& error : result.errors) {
                CHECK_FALSE(error.check.empty());
            }
        }
    }
}

#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <match/expr/diagnostic.hpp>

namespace match::cardschema {
using json = nlohmann::json;
using Diagnostic = match::expr::Diagnostic;

enum class Band { kBase = 0, kAttribute = 1, kOverride = 2, kRuntime = 3 };

// INFO: the deck budgets and the literal spellings of the selector forms and
// combine modes live in one place; the loader and later phases compare strings.
inline constexpr int kMinimumDeckCards = 1;
inline constexpr int kMaximumDeckCards = 1024;
inline constexpr int kMaximumDeckCopies = 1024;

namespace selector_mode {
inline constexpr const char* kAll = "all";
inline constexpr const char* kTag = "tag";
inline constexpr const char* kIdentifiers = "identifiers";
}  // namespace selector_mode

namespace combine_mode {
inline constexpr const char* kProduct = "product";
inline constexpr const char* kUniquePairs = "unique_pairs";
inline constexpr const char* kZip = "zip";
}  // namespace combine_mode

struct SurfaceDef { std::string mask; std::string channel; };  // channel: red|green|blue

struct Contribution {
    std::optional<std::string> sprite, tint, shader, when;
    json parameters = json::object();
    std::map<std::string, SurfaceDef> surfaces;
    Band band = Band::kBase;
    bool explicit_override = false;      // JSON key "explicit"
    std::string pointer;                 // JSON pointer of this contribution, for errors
};
using ContributionMap = std::map<std::string, Contribution>;  // slot name -> contribution

struct AttributeDef {
    std::string identifier;   // "uni:color"
    bool extensible = true;
    std::string visibility = "public";  // public|owner|hidden
    std::string file;
};

struct MemberDef {
    std::string attribute;    // "uni:color"
    std::string identifier;   // "uni:red"
    std::string display_name;
    std::vector<std::string> tags;
    int order = 0;
    ContributionMap contribute;
    json contribute_raw = json();       // parsed into `contribute` in a later step
    json properties = json::object();   // typed data, e.g. points
    std::string identifier_alias;
    std::string file;
};

struct SlotBinding {          // one entry of kind.attributes
    std::string attribute;    // "uni:color"
    std::string name;         // slot name; default: short attribute name
    std::string surface;      // optional
    std::string surface_pointer;   // JSON pointer of "surface", for errors
    std::string attribute_pointer;  // JSON pointer of "attribute", for errors
};

struct KindTemplate {
    std::string identifier;   // "uni:draw2"
    bool is_abstract = false;
    std::string extends;
    std::string extends_pointer;   // JSON pointer of the "extends" key, for diagnostics
    std::vector<std::string> tags;
    std::vector<SlotBinding> slots;
    std::vector<std::vector<std::string>> symmetric_groups;
    std::vector<std::string> slot_order;
    std::string identifier_format;      // empty: default format
    std::string title_format;           // empty: default title
    json face = json::object();         // transitional (Open question 5)
    json behavior = json();             // verbatim "behavior" object or null
    json window = json();
    json auto_trigger = json();
    std::string label_key, description_key;
    json properties = json::object();
    ContributionMap contribute;
    json contribute_raw = json();       // parsed into `contribute` in a later step
    std::string file;
};

struct MemberSelector {
    std::string mode;                    // selector_mode::k*
    std::string tag;
    std::vector<std::string> identifiers;
    std::string pointer;
};

struct DeckEntry {
    std::string kind;                    // template identifier
    json copies;                         // integer or expression string
    std::map<std::string, MemberSelector> members;   // slot name -> selector
    std::string combine = combine_mode::kProduct;    // combine_mode::k*
    std::string pointer;
};

struct DeckSpec {
    std::string identifier;
    std::string name;
    int max_cards = kMaximumDeckCards;
    ContributionMap contribute;          // back side
    std::vector<DeckEntry> cards;
    std::vector<std::string> mods;       // optional "mods" list, copied to DeckDef.mods
    std::string file;
};

struct PropertyDef {
    std::string identifier;
    std::string scope;                   // card|seat|match
    std::string type;                    // int|float|boolean|string|member
    json default_value;
    std::optional<std::pair<double, double>> range;
    std::string visibility = "public";
    bool sync_to_clients = true;
    std::vector<std::string> writable_by;   // empty means "owner"
    std::vector<std::string> values;        // string enum
    std::string member_attribute;           // type member
    std::string file;
};

struct ModSchema {
    std::string mod_id;
    std::string directory;
    std::vector<AttributeDef> attributes;
    std::vector<MemberDef> members;
    std::vector<KindTemplate> kinds;
    std::vector<DeckSpec> decks;
    std::vector<PropertyDef> properties;
};

std::string NamespaceOf(const std::string& identifier);   // "uni:red" -> "uni"
std::string LocalNameOf(const std::string& identifier);   // "uni:red" -> "red"
bool IsValidLocalName(const std::string& local);          // [a-z0-9_]{1,32}
bool IsValidIdentifier(const std::string& identifier);    // "<ns>:<local>"
}  // namespace match::cardschema

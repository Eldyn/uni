#include <match/expr/verify_command.hpp>

#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <match/expr/checker.hpp>
#include <match/expr/diagnostic.hpp>
#include <match/expr/schema_json.hpp>
#include <match/expr/types.hpp>

namespace match::expr {

namespace {

using json = nlohmann::json;

// INFO: the file label and pointer the verifier formats its diagnostics with;
// the corpus uses the same pair.
constexpr std::string_view kFile = "<expression>";
constexpr std::string_view kPointer = "/";

constexpr std::string_view kUsage =
    "usage: uni_mods verify --schema <file> --expression <text> [--expect <type>]\n";

std::optional<Type> TypeFromName(std::string_view name) {
    if (name == "bool") return Type::kBool;
    if (name == "int") return Type::kInt;
    if (name == "float") return Type::kFloat;
    if (name == "string") return Type::kString;
    if (name == "member_set") return Type::kMemberSet;
    if (name == "seat") return Type::kSeat;
    if (name == "seat_set") return Type::kSeatSet;
    if (name == "card") return Type::kCard;
    if (name == "card_set") return Type::kCardSet;
    return std::nullopt;
}

struct Options {
    std::string schema_path;
    std::string expression;
    std::optional<Type> expected;
    bool has_expression = false;
};

bool ParseArgs(const std::vector<std::string>& args, Options& options) {
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string& arg = args[index];
        if (arg == "--schema") {
            if (++index == args.size()) {
                return false;
            }
            options.schema_path = args[index];
        } else if (arg == "--expression") {
            if (++index == args.size()) {
                return false;
            }
            options.expression = args[index];
            options.has_expression = true;
        } else if (arg == "--expect") {
            if (++index == args.size()) {
                return false;
            }
            const std::optional<Type> expected = TypeFromName(args[index]);
            if (!expected.has_value()) {
                return false;
            }
            options.expected = expected;
        } else {
            return false;
        }
    }
    return !options.schema_path.empty() && options.has_expression;
}

}  // namespace

int RunVerify(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    Options options;
    if (!ParseArgs(args, options)) {
        err << kUsage;
        return 2;
    }

    std::ifstream stream(options.schema_path);
    if (!stream.good()) {
        err << "cannot read the schema file \"" + options.schema_path + "\"\n";
        return 2;
    }
    json value;
    try {
        stream >> value;
    } catch (const json::exception&) {
        err << "the schema file \"" + options.schema_path + "\" is not valid JSON\n";
        return 2;
    }

    std::string error;
    const std::optional<Schema> schema = LoadSchema(value, error);
    if (!schema.has_value()) {
        err << "invalid schema: " + error + "\n";
        return 2;
    }

    const CheckResult result =
        CheckExpression(options.expression, *schema, options.expected, kFile, kPointer);
    if (result.diagnostics.empty()) {
        out << "ok\n";
        return 0;
    }
    for (const Diagnostic& diagnostic : result.diagnostics) {
        err << FormatDiagnostic(diagnostic) << "\n";
    }
    return 1;
}

}  // namespace match::expr

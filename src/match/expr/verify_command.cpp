#include <match/expr/verify_command.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include <match/expr/checker.hpp>
#include <match/expr/diagnostic.hpp>
#include <match/expr/schema_json.hpp>
#include <match/expr/types.hpp>

namespace match::expr {

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

// INFO: the file label and pointer the verifier formats its diagnostics with;
// the corpus uses the same pair.
constexpr std::string_view kExpressionFileLabel = "<expression>";
constexpr std::string_view kRootPointer = "/";

constexpr std::uintmax_t kMaximumSchemaFileSize = 1024 * 1024;
constexpr std::size_t kMaximumExpressionBytes = 65536;

constexpr std::string_view kUsage =
    "usage: uni_mods verify --schema <file> --expression <text> [--expect <type>]\n";

struct Options {
    std::string schema_path;
    std::string expression;
    std::optional<Type> expected;
    bool has_schema = false;
    bool has_expression = false;
    bool has_expected = false;
};

// INFO: consumes the value that follows `flag`; `index` stops on it.
bool TakeValue(const std::vector<std::string>& args, std::size_t& index,
               std::string_view flag, std::string& value, std::string& error) {
    if (index + 1 >= args.size()) {
        error = std::string(flag) + " needs a value";
        return false;
    }
    value = args[++index];
    return true;
}

// INFO: returns the message for a bad command line, or an empty optional when
// the arguments are complete. A missing required flag has no message of its own.
std::optional<std::string> ParseArgs(const std::vector<std::string>& args, Options& options) {
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string& arg = args[index];
        std::string error;
        if (arg == "--schema") {
            if (options.has_schema) {
                return std::string("flag \"--schema\" given twice");
            }
            if (!TakeValue(args, index, "--schema", options.schema_path, error)) {
                return error;
            }
            options.has_schema = true;
        } else if (arg == "--expression") {
            if (options.has_expression) {
                return std::string("flag \"--expression\" given twice");
            }
            if (!TakeValue(args, index, "--expression", options.expression, error)) {
                return error;
            }
            options.has_expression = true;
        } else if (arg == "--expect") {
            if (options.has_expected) {
                return std::string("flag \"--expect\" given twice");
            }
            std::string name;
            if (!TakeValue(args, index, "--expect", name, error)) {
                return error;
            }
            const std::optional<Type> expected = TypeFromName(name);
            if (!expected.has_value()) {
                return "unknown type \"" + name + "\" for --expect";
            }
            options.expected = expected;
            options.has_expected = true;
        } else {
            return "unknown flag \"" + arg + "\"";
        }
    }
    if (!options.has_schema || !options.has_expression) {
        return std::string();
    }
    return std::nullopt;
}

// INFO: a regular file at `path` whose contents fit the size limit. Streams the
// reason to `err` and returns false otherwise. `scan` collects the repeated keys
// and reports a nesting past the scanner limit.
bool ReadSchemaFile(const std::string& path, json& value, SchemaScan& scan, std::ostream& err) {
    std::error_code status_error;
    const fs::file_status status = fs::status(path, status_error);
    if (status_error) {
        err << "cannot read \"" << path << "\": " << status_error.message() << "\n";
        return false;
    }
    if (!fs::is_regular_file(status)) {
        if (!fs::exists(status)) {
            err << "cannot read \"" << path << "\": " << std::strerror(ENOENT) << "\n";
        } else {
            err << "cannot read \"" << path << "\": not a regular file\n";
        }
        return false;
    }
    std::error_code size_error;
    const std::uintmax_t size = fs::file_size(path, size_error);
    if (size_error) {
        err << "cannot read \"" << path << "\": " << size_error.message() << "\n";
        return false;
    }
    if (size > kMaximumSchemaFileSize) {
        err << "schema file is too large\n";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        err << "cannot read \"" << path << "\": " << std::strerror(errno) << "\n";
        return false;
    }
    // INFO: read one byte past the limit rather than the whole file: the size
    // check above can be stale if the file grows between the two.
    std::string text(kMaximumSchemaFileSize + 1, '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(stream.gcount()));
    if (text.size() > kMaximumSchemaFileSize) {
        err << "schema file is too large\n";
        return false;
    }
    const std::optional<json> parsed = ParseSchemaJson(text, scan);
    if (!parsed.has_value()) {
        err << "the schema file \"" << path << "\" is not valid JSON\n";
        return false;
    }
    value = *parsed;
    return true;
}

int RunVerifyImpl(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << kUsage;
        return 2;
    }
    if (args[0] != "verify") {
        err << "unknown command \"" << args[0] << "\"\n" << kUsage;
        return 2;
    }
    Options options;
    if (const std::optional<std::string> error = ParseArgs(args, options)) {
        if (!error->empty()) {
            err << *error << "\n";
        }
        err << kUsage;
        return 2;
    }

    json value;
    SchemaScan scan;
    if (!ReadSchemaFile(options.schema_path, value, scan, err)) {
        return 3;
    }
    if (scan.nested_too_deep) {
        const Diagnostic nested{options.schema_path, std::string(kRootPointer),
                                "schema is nested too deeply (more than 64 levels)", ""};
        err << FormatDiagnostic(nested) << "\n";
        return 1;
    }

    std::vector<Diagnostic> schema_diagnostics;
    const std::optional<Schema> schema =
        LoadSchema(value, options.schema_path, scan, schema_diagnostics);
    if (!schema.has_value()) {
        for (const Diagnostic& diagnostic : schema_diagnostics) {
            err << FormatDiagnostic(diagnostic) << "\n";
        }
        return 1;
    }

    if (options.expression.size() > kMaximumExpressionBytes) {
        const Diagnostic too_long{std::string(kExpressionFileLabel),
                                  std::string(kRootPointer),
                                  "expression is too long (more than " +
                                      std::to_string(kMaximumExpressionBytes) + " bytes)",
                                  ""};
        err << FormatDiagnostic(too_long) << "\n";
        return 1;
    }

    const CheckResult result = CheckExpression(options.expression, *schema, options.expected,
                                               kExpressionFileLabel, kRootPointer);
    if (result.diagnostics.empty()) {
        out << "ok\n";
        return 0;
    }
    for (const Diagnostic& diagnostic : result.diagnostics) {
        err << FormatDiagnostic(diagnostic) << "\n";
    }
    return 1;
}

}  // namespace

int RunVerify(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    try {
        return RunVerifyImpl(args, out, err);
    } catch (const std::exception& error) {
        err << "internal error: " << error.what() << "\n";
        return 3;
    }
}

}  // namespace match::expr

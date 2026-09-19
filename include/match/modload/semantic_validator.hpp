#pragma once

#include "match/modload/artifacts.hpp"
#include "match/modload/restriction.hpp"

#include <map>
#include <string>
#include <vector>

/**
 * @file semantic_validator.hpp
 * @brief Semantic validation of the artifact model.
 *
 * Runs the ordered checks 1-8 over `LoadedMod` values (never re-reading JSON):
 * schema conformance, id syntax/uniqueness, reference resolution, op
 * arity/type, graph structure, mutation conflicts, deck validation and
 * selector sanity. The first failing check's errors are returned; the shape is
 * always `{check, artifact, path, message}`.
 *
 * Check ids (stable short strings):
 *   schema.invalid, schema.unsupported, schema.missing   (check 1)
 *   id.syntax, id.duplicate                              (check 2)
 *   ref.kind, ref.status, ref.tag, ref.restriction,
 *   ref.graph                                            (check 3)
 *   op.unknown, op.arity, op.type, op.bounds, graph.node,
 *   hook.name                                            (check 4)
 *   graph.cycle, graph.unreachable, graph.call_original,
 *   window.default                                       (check 5)
 *   mutation.conflict                                    (check 6)
 *   deck.kind, deck.count, deck.mod, deck.setting        (check 7)
 *   selector.unknown, selector.scope                     (check 8)
 */

namespace match::modload {

/** @brief Schema filenames inside the contract schema directory. */
namespace schema_files {
inline constexpr const char* kMod = "mod.schema.json";
inline constexpr const char* kCards = "cards.schema.json";
inline constexpr const char* kRules = "rules.schema.json";
inline constexpr const char* kMutations = "mutations.schema.json";
inline constexpr const char* kDeck = "deck.schema.json";
}  // namespace schema_files

/**
 * @struct SchemaViolation
 * @brief One schema failure: instance JSON path + human reason.
 */
struct SchemaViolation {
    std::string path;     /**< JSON-pointer-ish path into the instance. */
    std::string message;  /**< human-readable reason. */
};

/**
 * @class JsonSchemaSubsetValidator
 * @brief In-process draft-2020-12 SUBSET validator (no new dependency).
 *
 * Supported validation keywords: `type`, `required`, `properties`,
 * `additionalProperties`, `items`, `enum`, `const`, `pattern`,
 * `minLength`, `maxLength`, `minimum`, `maximum`, `oneOf`, `propertyNames`,
 * `$ref` (same-document `#/...` only). Structural/annotation keywords are
 * accepted and ignored: `$schema`, `$id`, `$defs`, `title`, `description`,
 * `default`, `examples`, `deprecated`, `readOnly`, `writeOnly`.
 *
 * NOTE: `propertyNames` is not in the brief's minimal list, but
 * `contract/schemas/deck.schema.json` uses it; without support every deck
 * would be rejected as an unsupported-schema error. It is therefore supported.
 *
 * Any other keyword makes `Compile` fail loudly ( check 1: an
 * unsupported keyword must never silently pass). Array-form `items` and
 * external `$ref` are unsupported and fail compilation.
 */
class JsonSchemaSubsetValidator {
   public:
    /** @brief Keywords this validator understands (validation + structural). */
    static const std::vector<std::string>& SupportedKeywords();

    /**
     * @brief Compile a schema document.
     * @return false with `error` set when a keyword is unsupported/malformed.
     */
    static bool Compile(const nlohmann::json& schema,
                        JsonSchemaSubsetValidator& out,
                        std::string& error);

    /** @brief Validate an instance; empty result means conformant. */
    std::vector<SchemaViolation> Validate(const nlohmann::json& instance) const;

   private:
    nlohmann::json root_ = nlohmann::json::object();
};

/**
 * @class SemanticValidator
 * @brief Ordered semantic checks over the loaded artifact model.
 */
class SemanticValidator {
   public:
    /** @brief A compiled schema (or the reason it could not compile). */
    struct CompiledSchema {
        bool ok = false;
        bool missing = false;
        std::string error;
        JsonSchemaSubsetValidator validator;
    };

    /**
     * @param schema_dir Directory holding `contract/schemas/*.schema.json`.
     *                   An empty or unreadable directory makes check 1 emit
     *                   `schema.missing`.
     */
    explicit SemanticValidator(std::string schema_dir);

    /**
     * @brief Checks 1-6 and 8 for one mod folder (check 7 runs on a deck).
     *
     * Cross-namespace references are deferred here (the active set is
     * unknown); local-namespace references resolve. `ValidateMatchSet`
     * resolves the full set.
     *
     * @return Errors of the first failing check; empty when valid.
     */
    std::vector<LoadError> ValidateMod(const LoadedMod& mod) const;

    /**
     * @brief Checks 1-8 across the active mod set plus the selected deck.
     *
     * @param mods The active mods, in lobby priority order.
     * @param deck The selected deck snapshot.
     * @return Errors of the first failing check; empty when valid.
     */
    std::vector<LoadError> ValidateMatchSet(
        const std::vector<LoadedMod>& mods,
        const DeckDef& deck) const;

    /** @brief Load + compile a schema by filename, caching the result. */
    const CompiledSchema& Schema(const std::string& filename) const;

   private:
    std::string schema_dir_;
    mutable std::map<std::string, CompiledSchema> schema_cache_;
};

}  // namespace match::modload

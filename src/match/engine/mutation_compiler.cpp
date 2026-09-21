#include "match/engine/mutation_compiler.hpp"

#include <logger.hpp>

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

/**
 * @file mutation_compiler.cpp
 * @brief Assembly-time mutation compiler.
 *
 * Node-id scheme: every node spliced in from a mutation is re-identified with
 * the prefix `m<index>_`, where `<index>` is the mutation's position in the
 * caller's ordered list. Intra-mutation route references are rewritten to the
 * new ids, so a fold never collides with the accumulated graph or with another
 * mutation. Synthetic helper nodes use the same prefix (`m<index>_wrap`,
 * `m<index>_veto`, `m<index>_veto_end`). Collisions with an author-chosen id
 * are broken with a numeric suffix.
 *
 * Veto shape: a `branch` head node whose single case tests the veto `where`;
 * on match it routes to an empty `fork` terminator (a no-op end), otherwise its
 * `else` routes to the accumulated graph's entry. Multiple vetoes AND together
 * because each fold prepends its own head in mod-list order.
 */

namespace match::engine {
namespace {

using nlohmann::json;

/** INFO: the reserved op name a `wrap` mutation uses as a splice marker. */
constexpr const char* kCallOriginalOp = "call_original";

/** INFO: top-level route keys that hold a single node id. */
constexpr const char* kSingleRoutes[] = {"next", "else", "default",
                                         "default_route"};

/** @brief The graph's declared node list, preferring `nodes` over `raw`. */
json NodesOf(const modload::BehaviorGraph& graph) {
    if (graph.nodes.is_array()) return graph.nodes;
    if (graph.raw.is_object() && graph.raw.contains("nodes")
        && graph.raw["nodes"].is_array()) {
        return graph.raw["nodes"];
    }
    return json::array();
}

/** @brief True when `node` is the `call_original` splice marker. */
bool IsMarker(const json& node) {
    return node.is_object()
        && node.value("op", std::string()) == kCallOriginalOp;
}

/** @brief True when `node` carries one of the Resolver's node kinds. */
bool IsRoutable(const json& node) {
    if (!node.is_object()) return false;
    return node.contains("op") || node.contains("cases")
        || node.contains("branches") || node.contains("window")
        || node.contains("schedule");
}

/** @brief Id of the first routable node, i.e. the graph's entry node. */
std::string EntryId(const json& nodes) {
    if (!nodes.is_array()) return std::string();
    for (const json& node : nodes) {
        if (!IsRoutable(node)) continue;
        std::string id = node.value("id", std::string());
        if (!id.empty()) return id;
    }
    return std::string();
}

/** @brief Every declared node id, for collision-free synthesis. */
std::set<std::string> CollectIds(const json& nodes) {
    std::set<std::string> ids;
    if (!nodes.is_array()) return ids;
    for (const json& node : nodes) {
        if (!node.is_object()) continue;
        std::string id = node.value("id", std::string());
        if (!id.empty()) ids.insert(id);
    }
    return ids;
}

/** @brief Rewrite one route value through `rename` when it names a node. */
void RewriteId(json& value, const std::map<std::string, std::string>& rename) {
    if (!value.is_string()) return;
    auto it = rename.find(value.get<std::string>());
    if (it != rename.end()) value = it->second;
}

/**
 * @brief Rewrite every routing reference in `node` through `rename`.
 *
 * Covers the router's vocabulary: single-route keys, a `fork` branch list, a
 * `branch` case list, an `on_response` map and a nested `window` object.
 */
void RewriteRoutes(json& node,
                   const std::map<std::string, std::string>& rename) {
    if (!node.is_object()) return;
    for (const char* key : kSingleRoutes) {
        auto it = node.find(key);
        if (it != node.end()) RewriteId(*it, rename);
    }
    auto branches = node.find("branches");
    if (branches != node.end() && branches->is_array()) {
        for (json& branch : *branches) RewriteId(branch, rename);
    }
    auto cases = node.find("cases");
    if (cases != node.end() && cases->is_array()) {
        for (json& entry : *cases) {
            if (!entry.is_object()) continue;
            auto next = entry.find("next");
            if (next != entry.end()) RewriteId(*next, rename);
        }
    }
    auto respond = node.find("on_response");
    if (respond != node.end() && respond->is_object()) {
        for (auto it = respond->begin(); it != respond->end(); ++it) {
            RewriteId(it.value(), rename);
        }
    }
    auto window = node.find("window");
    if (window != node.end() && window->is_object()) {
        for (const char* key : {"default", "default_route"}) {
            auto it = window->find(key);
            if (it != window->end()) RewriteId(*it, rename);
        }
    }
}

/** @brief Claim `wanted`, adding a `_N` suffix until it is unused. */
std::string UniqueId(const std::string& wanted, std::set<std::string>& used) {
    std::string candidate = wanted.empty() ? std::string("node") : wanted;
    if (used.insert(candidate).second) return candidate;
    for (int suffix = 2;; ++suffix) {
        std::string next = candidate + "_" + std::to_string(suffix);
        if (used.insert(next).second) return next;
    }
}

/** @brief One mutation's node list, re-identified and route-rewritten. */
struct Prepared {
    json nodes = json::array();                /**< fresh-id copies. */
    std::string entry;                         /**< id of `nodes.front()`. */
    std::vector<std::size_t> marker_indices;   /**< `call_original` positions. */
};

/**
 * @brief Copy `source` with unique ids under `prefix` and rewritten routes.
 *
 * Non-object entries are dropped (malformed JSON nodes cannot be spliced).
 * `used` is grown with every chosen id so callers can keep synthesising.
 */
Prepared Prepare(const json& source, const std::string& prefix,
                 std::set<std::string>& used) {
    Prepared out;
    if (!source.is_array()) return out;

    std::vector<std::pair<std::string, std::string>> ids;
    std::vector<std::size_t> kept;
    ids.reserve(source.size());
    kept.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        const json& node = source[index];
        if (!IsRoutable(node)) {
            Logger::Warn("[MutationCompiler] dropped malformed node while "
                         "splicing a mutation graph");
            continue;
        }
        std::string old = node.value("id", std::string());
        std::string base = prefix + (old.empty() ? std::string("anon") : old);
        ids.emplace_back(old, UniqueId(base, used));
        kept.push_back(index);
    }

    std::map<std::string, std::string> rename;
    for (const auto& pair : ids) {
        if (!pair.first.empty()) rename[pair.first] = pair.second;
    }

    for (std::size_t i = 0; i < kept.size(); ++i) {
        const json& node = source[kept[i]];
        json copy = node;
        copy["id"] = ids[i].second;
        RewriteRoutes(copy, rename);
        if (IsMarker(copy)) out.marker_indices.push_back(out.nodes.size());
        out.nodes.push_back(std::move(copy));
    }
    out.entry = EntryId(out.nodes);
    return out;
}

/**
 * @brief Replace each `call_original` marker with a fork that runs the original.
 *
 * The marker becomes `{id, branches:[original_entry], next:<marker next>}` so
 * the accumulated graph runs in place of the marker and control returns to the
 * injected continuation. Only the first marker inlines the original; any extra
 * markers become pass-through forks (empty branch list) so a malformed graph
 * still walks deterministically.
 */
void SpliceMarkers(Prepared& injected, const std::string& original_entry) {
    bool first = true;
    for (std::size_t index : injected.marker_indices) {
        json& marker = injected.nodes[index];
        const std::string id = marker.value("id", std::string());
        const std::string next = marker.value("next", std::string());
        json fork = json::object();
        fork["id"] = id;
        fork["branches"] = json::array();
        if (first && !original_entry.empty()) {
            fork["branches"].push_back(original_entry);
        }
        if (!next.empty()) fork["next"] = next;
        marker = std::move(fork);
        first = false;
    }
}

/** @brief Fold one `veto` into `current` as a head guard. */
json ApplyVeto(const json& current, const modload::MutationDef& mutation,
               std::size_t index) {
    if (!mutation.where.has_value()) {
        Logger::Warn("[MutationCompiler] veto '", mutation.mutation_id,
                     "' has no 'where'; inert");
        return current;
    }
    const std::string prefix = "m" + std::to_string(index) + "_";
    std::set<std::string> used = CollectIds(current);
    const std::string head_id = UniqueId(prefix + "veto", used);
    const std::string end_id = UniqueId(prefix + "veto_end", used);

    json head = json::object();
    head["id"] = head_id;
    head["cases"] = json::array(
        {json{{"when", *mutation.where}, {"next", end_id}}});
    head["else"] = EntryId(current);

    json terminator = json::object();
    terminator["id"] = end_id;
    terminator["branches"] = json::array();

    json nodes = json::array();
    nodes.push_back(std::move(head));
    for (const json& node : current) nodes.push_back(node);
    nodes.push_back(std::move(terminator));
    return nodes;
}

/** @brief Fold one `wrap` into `current`, splicing `call_original`. */
json ApplyWrap(const json& current, const modload::MutationDef& mutation,
               std::size_t index) {
    json injected_nodes = NodesOf(mutation.replacement);
    if (!injected_nodes.is_array() || injected_nodes.empty()) {
        Logger::Warn("[MutationCompiler] wrap '", mutation.mutation_id,
                     "' has no replacement nodes; inert");
        return current;
    }
    const std::string prefix = "m" + std::to_string(index) + "_";
    std::set<std::string> used = CollectIds(current);
    Prepared injected = Prepare(injected_nodes, prefix, used);
    if (injected.nodes.empty()) {
        Logger::Warn("[MutationCompiler] wrap '", mutation.mutation_id,
                     "' replacement has no usable nodes; inert");
        return current;
    }

    const std::string original_entry = EntryId(current);
    // INFO: a marker pins the original's position; `position` only decides
    //       marker-less wraps.
    if (!injected.marker_indices.empty()) {
        SpliceMarkers(injected, original_entry);
        json nodes = injected.nodes;
        for (const json& node : current) nodes.push_back(node);
        return nodes;
    }

    if (current.empty()) return injected.nodes;

    const bool after = mutation.position.value_or("before") == "after";
    json fork = json::object();
    fork["id"] = UniqueId(prefix + "wrap", used);
    fork["branches"] = json::array();
    if (after) {
        if (!original_entry.empty()) fork["branches"].push_back(original_entry);
        fork["branches"].push_back(injected.entry);
    } else {
        fork["branches"].push_back(injected.entry);
        if (!original_entry.empty()) fork["branches"].push_back(original_entry);
    }

    json nodes = json::array();
    nodes.push_back(std::move(fork));
    if (after) {
        for (const json& node : current) nodes.push_back(node);
        for (const json& node : injected.nodes) nodes.push_back(node);
    } else {
        for (const json& node : injected.nodes) nodes.push_back(node);
        for (const json& node : current) nodes.push_back(node);
    }
    return nodes;
}

}  // namespace

modload::BehaviorGraph CompileMutations(
    const modload::BehaviorGraph& original,
    const std::vector<const modload::MutationDef*>& mutations,
    const MutationCompileOptions& options) {
    json current = NodesOf(original);
    if (!current.is_array()) current = json::array();

    for (std::size_t index = 0; index < mutations.size(); ++index) {
        const modload::MutationDef* mutation = mutations[index];
        if (mutation == nullptr) {
            Logger::Warn("[MutationCompiler] null mutation at index ", index);
            continue;
        }
        if (options.restriction_targets.count(mutation->target) != 0) {
            Logger::Warn("[MutationCompiler] mutation '", mutation->mutation_id,
                         "' targets restriction entry '", mutation->target,
                         "'; inert");
            continue;
        }

        const std::string& mode = mutation->mode;
        if (mode == "veto") {
            current = ApplyVeto(current, *mutation, index);
        } else if (mode == "replace") {
            json replacement = NodesOf(mutation->replacement);
            if (!replacement.is_array() || replacement.empty()) {
                Logger::Warn("[MutationCompiler] replace '",
                             mutation->mutation_id,
                             "' has no replacement nodes; inert");
                continue;
            }
            current = std::move(replacement);
        } else if (mode == "wrap") {
            current = ApplyWrap(current, *mutation, index);
        } else if (mode == "filter") {
            Logger::Warn("[MutationCompiler] mutation '", mutation->mutation_id,
                         "' mode 'filter' is deferred; inert");
        } else {
            Logger::Warn("[MutationCompiler] mutation '", mutation->mutation_id,
                         "' has unknown mode '", mode, "'; inert");
        }
    }

    modload::BehaviorGraph out;
    out.nodes = std::move(current);
    out.raw = json{{"nodes", out.nodes}};
    return out;
}

}  // namespace match::engine

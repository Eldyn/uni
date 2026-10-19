#include <match/cardschema/catalog.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <match/cardschema/identifier.hpp>
#include <match/cardschema/resolve.hpp>
#include <match/expr/checker.hpp>
#include <match/expr/diagnostic.hpp>
#include <match/expr/evaluator.hpp>
#include <match/expr/parser.hpp>

namespace match::cardschema {

namespace {

// INFO: the catalog reports at most this many diagnostics; a hostile mod set
// cannot flood the report.
constexpr std::size_t kMaximumDiagnostics = 100;

// INFO: an attribute with more members than this is a loader bug or a hostile
// input; the whole attribute is dropped and reported once.
constexpr std::size_t kMaximumMembersPerAttribute = 1024;

// INFO: a host mod may generate at most this many cards; the collapsed product
// of the member counts is checked against it before any card is allocated.
constexpr std::uint64_t kMaximumGeneratedKinds = 4096;

// INFO: the count of a rejected template; it is above every cap, so it consumes
// the host budget for the templates that follow.
constexpr std::uint64_t kOverBudgetCount = kMaximumGeneratedKinds + 1;

void AddError(std::vector<Diagnostic>& diagnostics, const std::string& file,
              const std::string& pointer, std::string message, std::string suggestion = {}) {
    if (diagnostics.size() >= kMaximumDiagnostics) {
        return;
    }
    diagnostics.push_back(
        Diagnostic{file, pointer, std::move(message), std::move(suggestion)});
}

// INFO: the name a member contributes to an identifier and a title: its alias
// when it sets one, otherwise the local part of its identifier.
std::string MemberLocal(const MemberDef& member) {
    if (!member.identifier_alias.empty()) {
        return member.identifier_alias;
    }
    return LocalNameOf(member.identifier);
}

std::string CapitalizeFirst(std::string value) {
    if (!value.empty() && value.front() >= 'a' && value.front() <= 'z') {
        value.front() = static_cast<char>(value.front() - 'a' + 'A');
    }
    return value;
}

std::vector<std::string> SplitWords(const std::string& text) {
    std::vector<std::string> words;
    std::string word;
    for (const char character : text) {
        if (character == '_') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
            continue;
        }
        word += character;
    }
    if (!word.empty()) {
        words.push_back(word);
    }
    return words;
}

std::string KindPointer(const char* key) {
    return match::expr::AppendPointerToken(
        match::expr::AppendPointerToken(match::expr::kRootPointer, "kind"), key);
}

// INFO: the pointer of the field that produced a generated id: the explicit
// format when the kind sets one, the default-format key otherwise.
std::string IdentifierPointer(const KindTemplate& kind) {
    return KindPointer(kind.identifier_format.empty() ? "identifier" : "identifier_format");
}

// INFO: the fallback pointer of an `attributes` binding for a kind a caller
// built in memory: the position of the binding in `kind.slots`. A loaded kind
// records the real file pointer instead, because the loader skips rejected
// bindings and the position can drift from the file's array index.
std::string AttributePointer(std::size_t index) {
    std::string pointer = match::expr::AppendPointerToken(match::expr::kRootPointer, "kind");
    pointer = match::expr::AppendPointerToken(pointer, "attributes");
    pointer = match::expr::AppendPointerToken(pointer, std::to_string(index));
    return match::expr::AppendPointerToken(pointer, "attribute");
}

std::string AttributePointerFor(const KindTemplate& kind, std::size_t index) {
    const SlotBinding& binding = kind.slots[index];
    if (!binding.attribute_pointer.empty()) {
        return binding.attribute_pointer;
    }
    return AttributePointer(index);
}

const SlotBinding* FindBinding(const KindTemplate& kind, const std::string& name) {
    for (const SlotBinding& binding : kind.slots) {
        if (binding.name == name) {
            return &binding;
        }
    }
    return nullptr;
}

// INFO: the slots the expansion crosses: the declared `slot_order` first, then
// every other declared binding in file order, so a slot the kind does not order
// is not dropped from the cross product.
std::vector<std::string> EffectiveSlotOrder(const KindTemplate& kind) {
    std::vector<std::string> order;
    const auto append = [&order, &kind](const std::string& name) {
        if (name.empty() || std::find(order.begin(), order.end(), name) != order.end()) {
            return;
        }
        for (const SlotBinding& binding : kind.slots) {
            if (binding.name == name) {
                order.push_back(name);
                return;
            }
        }
    };
    for (const std::string& name : kind.slot_order) {
        append(name);
    }
    for (const SlotBinding& binding : kind.slots) {
        append(binding.name);
    }
    return order;
}

void AppendTitleWords(std::string& title, const std::string& text) {
    for (const std::string& word : SplitWords(text)) {
        if (!title.empty()) {
            title += ' ';
        }
        title += CapitalizeFirst(word);
    }
}

// INFO: the default title: the template's local name split on "_" and
// capitalized, then every member value split on "_" the same way and appended
// ("Numbered Red Draw Two").
std::string DefaultTitle(const KindTemplate& kind, const std::vector<std::string>& slot_order,
                         const std::map<std::string, std::string>& slot_values) {
    std::string title;
    AppendTitleWords(title, LocalNameOf(kind.identifier));
    for (const std::string& slot : slot_order) {
        const auto found = slot_values.find(slot);
        if (found != slot_values.end()) {
            AppendTitleWords(title, found->second);
        }
    }
    return title;
}

// INFO: every string value of the face object, recursive, is filled with the
// member locals; a non-string value (a number, a boolean) is copied verbatim.
// Returns false as soon as one string cannot be filled, so a caller discards the
// card instead of emitting a half-filled face.
bool FillFaceStrings(json& node, const std::string& pointer,
                     const std::map<std::string, std::string>& slot_values,
                     const std::string& file, std::vector<Diagnostic>& diagnostics) {
    if (node.is_string()) {
        const std::string text = node.get<std::string>();
        const std::optional<std::string> filled =
            FillTemplate(text, slot_values, file, pointer, diagnostics);
        if (!filled) {
            return false;
        }
        node = *filled;
        return true;
    }
    if (node.is_object()) {
        for (auto entry = node.begin(); entry != node.end(); ++entry) {
            const std::string child_pointer =
                match::expr::AppendPointerToken(pointer, entry.key());
            if (!FillFaceStrings(entry.value(), child_pointer, slot_values, file, diagnostics)) {
                return false;
            }
        }
        return true;
    }
    if (node.is_array()) {
        for (std::size_t index = 0; index < node.size(); ++index) {
            if (!FillFaceStrings(node[index],
                                 match::expr::AppendPointerToken(pointer, std::to_string(index)),
                                 slot_values, file, diagnostics)) {
                return false;
            }
        }
    }
    return true;
}

// INFO: the placeholder grammar of every string of a face object; the pointer of
// a bad format is the JSON pointer of the string that holds it.
bool CheckTreeFormatSyntax(const json& node, const std::string& pointer, const std::string& file,
                           std::vector<Diagnostic>& diagnostics) {
    if (node.is_string()) {
        if (const std::optional<std::string> error =
                ValidateFormatSyntax(node.get<std::string>())) {
            AddError(diagnostics, file, pointer, *error);
            return false;
        }
        return true;
    }
    if (node.is_object()) {
        for (auto entry = node.begin(); entry != node.end(); ++entry) {
            if (!CheckTreeFormatSyntax(entry.value(),
                                       match::expr::AppendPointerToken(pointer, entry.key()), file,
                                       diagnostics)) {
                return false;
            }
        }
        return true;
    }
    if (node.is_array()) {
        for (std::size_t index = 0; index < node.size(); ++index) {
            if (!CheckTreeFormatSyntax(node[index],
                                       match::expr::AppendPointerToken(pointer,
                                                                       std::to_string(index)),
                                       file, diagnostics)) {
                return false;
            }
        }
    }
    return true;
}

// INFO: the syntax of every format a template owns, checked once per template so
// a broken format is reported once instead of once per generated card; a
// template with a bad format is skipped before any cross-product entry is built.
bool CheckFormatSyntax(const KindTemplate& kind, std::vector<Diagnostic>& diagnostics) {
    bool well_formed = true;
    if (!kind.identifier_format.empty()) {
        if (const std::optional<std::string> error = ValidateFormatSyntax(kind.identifier_format)) {
            AddError(diagnostics, kind.file, KindPointer("identifier_format"), *error);
            well_formed = false;
        }
    }
    if (!kind.title_format.empty()) {
        if (const std::optional<std::string> error = ValidateFormatSyntax(kind.title_format)) {
            AddError(diagnostics, kind.file, KindPointer("title_format"), *error);
            well_formed = false;
        }
    }
    if (!CheckTreeFormatSyntax(kind.face, KindPointer("face"), kind.file, diagnostics)) {
        well_formed = false;
    }
    return well_formed;
}

// INFO: the product of two counts, or nullopt when the true product does not fit
// in 64 bits. The caller compares the product against the 4096 cap, so an
// overflowing product is reported as "more than 4096".
std::optional<std::uint64_t> MultiplyCounts(std::uint64_t left, std::uint64_t right) {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return std::nullopt;
    }
    return left * right;
}

std::optional<std::uint64_t> MultiplyOptional(std::optional<std::uint64_t> left,
                                              std::optional<std::uint64_t> right) {
    if (!left || !right) {
        return std::nullopt;
    }
    return MultiplyCounts(*left, *right);
}

// INFO: the number of distinct assignments a symmetric group yields after the
// collapse: C(member_count + slot_count - 1, slot_count), the number of
// multisets of `slot_count` member names drawn from `member_count` members.
std::optional<std::uint64_t> SymmetricCollapseCount(std::uint64_t member_count,
                                                    std::uint64_t slot_count) {
    std::optional<std::uint64_t> result = 1;
    for (std::uint64_t step = 1; step <= slot_count; ++step) {
        if (!result) {
            break;
        }
        result = MultiplyCounts(*result, member_count + step - 1);
        if (result) {
            *result /= step;
        }
    }
    return result;
}

// INFO: the number of distinct cards the cross product yields after the
// symmetric collapse: a group of `slot_count` slots over one attribute of
// `member_count` members contributes C(member_count + slot_count - 1, slot_count)
// and every other slot contributes its member count. nullopt means the product
// overflows 64 bits, which is above the cap in any case. Checked before any card
// is allocated.
std::optional<std::uint64_t> CollapsedProduct(
    const KindTemplate& kind, const std::vector<std::string>& slot_order,
    const std::map<std::string, std::size_t>& member_counts) {
    std::set<std::string> grouped_slots;
    std::optional<std::uint64_t> total = std::uint64_t{1};
    for (const std::vector<std::string>& group : kind.symmetric_groups) {
        std::vector<std::string> present;
        for (const std::string& slot : group) {
            if (member_counts.count(slot) != 0) {
                present.push_back(slot);
            }
        }
        if (present.empty()) {
            continue;
        }
        const SlotBinding* first_binding = FindBinding(kind, present.front());
        const bool one_attribute = first_binding != nullptr &&
                                   std::all_of(present.begin(), present.end(),
                                               [&kind, first_binding](const std::string& slot) {
                                                   const SlotBinding* binding =
                                                       FindBinding(kind, slot);
                                                   return binding != nullptr &&
                                                          binding->attribute ==
                                                              first_binding->attribute;
                                               });
        std::optional<std::uint64_t> factor = std::uint64_t{1};
        if (one_attribute) {
            factor = SymmetricCollapseCount(
                static_cast<std::uint64_t>(member_counts.at(present.front())), present.size());
        } else {
            for (const std::string& slot : present) {
                factor = MultiplyOptional(
                    factor, static_cast<std::uint64_t>(member_counts.at(slot)));
            }
        }
        total = MultiplyOptional(total, factor);
        for (const std::string& slot : present) {
            grouped_slots.insert(slot);
        }
    }
    for (const std::string& slot : slot_order) {
        if (grouped_slots.count(slot) != 0) {
            continue;
        }
        const auto found = member_counts.find(slot);
        total = MultiplyOptional(
            total, found == member_counts.end() ? std::optional<std::uint64_t>{0}
                                                : std::optional<std::uint64_t>{found->second});
    }
    return total;
}

std::string DescribeMembers(const std::vector<std::string>& slot_order,
                            const std::map<std::string, std::string>& slot_values) {
    std::string text;
    for (const std::string& slot : slot_order) {
        const auto found = slot_values.find(slot);
        if (found == slot_values.end()) {
            continue;
        }
        if (!text.empty()) {
            text += ", ";
        }
        text += slot + "=" + found->second;
    }
    return text;
}

// INFO: the member a slot holds after canonicalization: the member of the
// slot's attribute whose local name is the canonical value. nullopt when the
// slot or the member is unknown to this kind.
const MemberDef* ResolveSlotMember(
    const KindTemplate& kind, const std::string& slot,
    const std::map<std::string, std::string>& canonical,
    const std::map<std::pair<std::string, std::string>, const MemberDef*>& members_by_attribute) {
    const auto local = canonical.find(slot);
    if (local == canonical.end()) {
        return nullptr;
    }
    const SlotBinding* binding = FindBinding(kind, slot);
    if (binding == nullptr) {
        return nullptr;
    }
    const auto member =
        members_by_attribute.find(std::make_pair(binding->attribute, local->second));
    return member == members_by_attribute.end() ? nullptr : member->second;
}

// INFO: visits every element of the cross product of `member_lists` in row-major
// order, the last list varying fastest; a template with no slots visits once with
// an empty assignment. The caller bounds the work: the collapsed product is
// checked against the cap before this loop runs.
template <typename Visitor>
void ForEachAssignment(const std::vector<std::vector<const MemberDef*>>& member_lists,
                       Visitor&& visit) {
    if (member_lists.empty()) {
        visit(std::vector<const MemberDef*>{});
        return;
    }
    std::vector<std::size_t> indices(member_lists.size(), 0);
    while (true) {
        std::vector<const MemberDef*> assignment;
        assignment.reserve(member_lists.size());
        for (std::size_t index = 0; index < member_lists.size(); ++index) {
            assignment.push_back(member_lists[index][indices[index]]);
        }
        visit(assignment);

        bool advanced = false;
        for (std::size_t position = indices.size(); position-- > 0;) {
            if (++indices[position] < member_lists[position].size()) {
                advanced = true;
                break;
            }
            indices[position] = 0;
        }
        if (!advanced) {
            break;
        }
    }
}

// INFO: builds one card from a canonical slot assignment. Returns nullopt when a
// filled format or face string was invalid; the diagnostics are already recorded
// then. The caller relies on this flag, not on the diagnostics vector, because
// that vector stops growing at the diagnostic cap.
std::optional<GeneratedCard> BuildCard(
    const KindTemplate& kind, const std::string& kind_id,
    const std::vector<std::string>& slot_order,
    const std::map<std::string, std::string>& canonical,
    const std::map<std::pair<std::string, std::string>, const MemberDef*>& members_by_attribute,
    std::vector<Diagnostic>& diagnostics) {
    GeneratedCard card;
    card.kind_id = kind_id;
    card.host_mod = NamespaceOf(kind.identifier);
    card.template_id = kind.identifier;

    std::map<std::string, const MemberDef*> assignment;
    for (const std::string& slot : slot_order) {
        const MemberDef* member = ResolveSlotMember(kind, slot, canonical, members_by_attribute);
        if (member == nullptr) {
            continue;
        }
        assignment[slot] = member;
        card.slot_members[slot] = member->identifier;
    }

    std::set<std::string> used_mods;
    for (const auto& entry : assignment) {
        used_mods.insert(NamespaceOf(entry.second->identifier));
    }
    used_mods.erase(card.host_mod);
    card.requires_mods.assign(used_mods.begin(), used_mods.end());

    std::set<std::string> seen_tags;
    for (const std::string& tag : kind.tags) {
        if (seen_tags.insert(tag).second) {
            card.tags.push_back(tag);
        }
    }
    for (const std::string& slot : slot_order) {
        const auto found = assignment.find(slot);
        if (found == assignment.end()) {
            continue;
        }
        for (const std::string& tag : found->second->tags) {
            if (seen_tags.insert(tag).second) {
                card.tags.push_back(tag);
            }
        }
    }

    if (kind.title_format.empty()) {
        card.title = DefaultTitle(kind, slot_order, canonical);
    } else {
        const std::optional<std::string> title =
            FillTitle(kind.title_format, canonical, kind.file, KindPointer("title_format"),
                      diagnostics);
        if (!title) {
            return std::nullopt;
        }
        card.title = *title;
    }

    card.face = kind.face;
    if (!FillFaceStrings(card.face, KindPointer("face"), canonical, kind.file, diagnostics)) {
        return std::nullopt;
    }

    json entry = json::object();
    entry["id"] = LocalNameOf(kind_id);
    entry["title"] = card.title;
    entry["face"] = card.face;
    entry["tags"] = card.tags;
    if (!kind.behavior.is_null()) {
        entry["behavior"] = kind.behavior;
    }
    if (!kind.window.is_null()) {
        entry["window"] = kind.window;
    }
    if (!kind.auto_trigger.is_null()) {
        entry["auto_trigger"] = kind.auto_trigger;
    }
    if (!kind.label_key.empty()) {
        entry["label_key"] = kind.label_key;
    }
    if (!kind.description_key.empty()) {
        entry["description_key"] = kind.description_key;
    }
    card.legacy_entry = std::move(entry);

    return card;
}

void AddDiagnostic(std::vector<Diagnostic>& diagnostics, const Diagnostic& diagnostic) {
    if (diagnostics.size() >= kMaximumDiagnostics) {
        return;
    }
    diagnostics.push_back(diagnostic);
}

// INFO: the deck of `deck_id` across every mod, or nullptr when no loaded mod
// declares it.
const DeckSpec* FindDeck(const std::vector<ModSchema>& mods, const std::string& deck_id) {
    for (const ModSchema& mod : mods) {
        for (const DeckSpec& deck : mod.decks) {
            if (deck.identifier == deck_id) {
                return &deck;
            }
        }
    }
    return nullptr;
}

// INFO: the deck member order: `order`, then identifier.
bool CompareMembers(const MemberDef* left, const MemberDef* right) {
    if (left->order != right->order) {
        return left->order < right->order;
    }
    return left->identifier < right->identifier;
}

// INFO: resolves one deck selector against the members of a slot's attribute.
// `all` and a missing selector take every member, `tag` the members that carry
// the tag, `identifiers` exactly the named members in stable order. An
// identifier that names no member of the attribute is a diagnostic with a
// suggestion; a selector that matches nothing leaves `selected` empty and is
// not an error. Returns false when a named member is unknown.
bool SelectMembers(const MemberSelector& selector, const std::string& attribute,
                   const std::vector<const MemberDef*>& members, const std::string& file,
                   std::vector<Diagnostic>& diagnostics,
                   std::vector<const MemberDef*>& selected) {
    if (selector.mode.empty() || selector.mode == selector_mode::kAll) {
        selected = members;
        return true;
    }
    if (selector.mode == selector_mode::kTag) {
        for (const MemberDef* member : members) {
            if (std::find(member->tags.begin(), member->tags.end(), selector.tag) !=
                member->tags.end()) {
                selected.push_back(member);
            }
        }
        return true;
    }
    if (selector.mode == selector_mode::kIdentifiers) {
        std::vector<std::string> candidates;
        candidates.reserve(members.size());
        for (const MemberDef* member : members) {
            candidates.push_back(member->identifier);
        }
        bool usable = true;
        for (const std::string& identifier : selector.identifiers) {
            const MemberDef* found = nullptr;
            for (const MemberDef* member : members) {
                if (member->identifier == identifier) {
                    found = member;
                    break;
                }
            }
            if (found == nullptr) {
                AddError(diagnostics, file, selector.pointer,
                         "unknown member \"" + identifier + "\" of attribute \"" + attribute +
                             "\"",
                         match::expr::SuggestClosest(identifier, candidates));
                usable = false;
                continue;
            }
            selected.push_back(found);
        }
        std::sort(selected.begin(), selected.end(), CompareMembers);
        return usable;
    }
    return true;
}

// INFO: the number of combinations a combine mode yields over the selected
// member lists. A zip whose lists differ in length and a unique_pairs over any
// other than two slots are reported here and give nullopt; a product count that
// overflows 64 bits is nullopt too, which is above every deck limit. The count
// is computed before any combination is built.
std::optional<std::uint64_t> ResolveCombinationCount(
    const std::string& combine, const std::vector<std::vector<const MemberDef*>>& lists,
    const std::string& file, const std::string& pointer, std::vector<Diagnostic>& diagnostics) {
    if (combine == combine_mode::kZip) {
        const std::size_t length = lists.empty() ? 1 : lists.front().size();
        for (const auto& list : lists) {
            if (list.size() != length) {
                AddError(diagnostics, file, pointer,
                         "zip combine needs equal slot lengths, got " +
                             std::to_string(lists.front().size()) + " and " +
                             std::to_string(list.size()));
                return std::nullopt;
            }
        }
        return static_cast<std::uint64_t>(length);
    }
    if (combine == combine_mode::kUniquePairs) {
        if (lists.size() != 2) {
            AddError(diagnostics, file, pointer,
                     "unique_pairs combine needs exactly two slots, got " +
                         std::to_string(lists.size()));
            return std::nullopt;
        }
        const std::uint64_t count = static_cast<std::uint64_t>(lists.front().size());
        // INFO: a pair count that overflows 64 bits is above every deck limit;
        // the saturated value lets the caller report the cap without the math.
        const std::optional<std::uint64_t> pairs = MultiplyCounts(count, count - 1);
        return pairs ? *pairs / 2 : std::numeric_limits<std::uint64_t>::max();
    }
    std::optional<std::uint64_t> total = 1;
    for (const auto& list : lists) {
        total = MultiplyOptional(total, static_cast<std::uint64_t>(list.size()));
    }
    return total;
}

// INFO: visits every combination the combine mode yields, in stable order. A
// product crosses the lists row-major, a zip pairs the lists element-wise (all
// equal length), and unique_pairs walks the pairs i < j of the first list, so
// the two slots share one member list. The caller bounds the work: the
// combination count was checked against the deck limit first.
template <typename Visitor>
void ForEachCombination(const std::string& combine,
                        const std::vector<std::vector<const MemberDef*>>& lists,
                        Visitor&& visit) {
    if (combine == combine_mode::kZip) {
        if (lists.empty()) {
            visit(std::vector<const MemberDef*>{});
            return;
        }
        for (std::size_t index = 0; index < lists.front().size(); ++index) {
            std::vector<const MemberDef*> assignment;
            assignment.reserve(lists.size());
            for (const auto& list : lists) {
                assignment.push_back(list[index]);
            }
            visit(assignment);
        }
        return;
    }
    if (combine == combine_mode::kUniquePairs) {
        const std::vector<const MemberDef*>& list = lists.front();
        for (std::size_t left = 0; left < list.size(); ++left) {
            for (std::size_t right = left + 1; right < list.size(); ++right) {
                visit(std::vector<const MemberDef*>{list[left], list[right]});
            }
        }
        return;
    }
    ForEachAssignment(lists, std::forward<Visitor>(visit));
}

// INFO: the runtime an expression context of a `copies` expression: only
// `@settings.<key>` resolves, to the int the settings map holds; every other
// selector, property, variable and query is empty or false. `Evaluate` calls
// SelectorValue then ReadProperty for `@settings.key`, so those are the two
// calls that matter.
class SettingsContext final : public match::expr::EvalContext {
public:
    explicit SettingsContext(const std::map<std::string, int64_t>& settings)
        : settings_(settings) {}

    std::vector<match::expr::EntityRef> ResolveSelector(std::string_view name) const override {
        if (name == "settings") {
            return {match::expr::EntityRef{match::expr::Type::kSettings, 0}};
        }
        return {};
    }

    std::optional<match::expr::Value> ReadProperty(match::expr::EntityRef entity,
                                                   std::string_view key) const override {
        if (entity.kind != match::expr::Type::kSettings) {
            return std::nullopt;
        }
        const auto found = settings_.find(std::string(key));
        if (found == settings_.end()) {
            return std::nullopt;
        }
        return match::expr::Value(static_cast<int64_t>(found->second));
    }

    bool WriteProperty(match::expr::EntityRef, std::string_view,
                       const match::expr::Value&) override {
        return false;
    }

    std::optional<match::expr::Value> GetVariable(std::string_view) const override {
        return std::nullopt;
    }

    bool HasTag(match::expr::EntityRef, std::string_view) const override { return false; }

    int64_t Random(int64_t low, int64_t) override { return low; }

    int64_t StatusMagnitude(match::expr::EntityRef, std::string_view) const override {
        return 0;
    }

private:
    const std::map<std::string, int64_t>& settings_;
};

// INFO: the copies of one deck entry: an integer in range, or an expression
// parsed, checked against a Schema whose settings are all ints, and evaluated
// against `settings`. Any problem is reported at the entry pointer (the
// expression check at the "copies" pointer) and gives nullopt, so the entry is
// skipped.
std::optional<int> ResolveCopies(const DeckEntry& entry, const DeckSpec& deck,
                                 const std::map<std::string, int64_t>& settings,
                                 std::vector<Diagnostic>& diagnostics) {
    if (entry.copies.is_number_integer()) {
        const long long number =
            entry.copies.is_number_unsigned()
                ? static_cast<long long>(entry.copies.get<unsigned long long>())
                : entry.copies.get<long long>();
        if (number < 0) {
            AddError(diagnostics, deck.file, entry.pointer,
                     "copies is " + std::to_string(number) +
                         "; the count must not be negative");
            return std::nullopt;
        }
        return static_cast<int>(number);
    }
    if (!entry.copies.is_string()) {
        AddError(diagnostics, deck.file, entry.pointer,
                 "key \"copies\" must be an integer or a non-empty string");
        return std::nullopt;
    }
    const std::string source = entry.copies.get<std::string>();
    const std::string pointer = match::expr::AppendPointerToken(entry.pointer, "copies");
    const match::expr::ParseResult parsed = match::expr::ParseExpression(source);
    if (parsed.error) {
        AddError(diagnostics, deck.file, pointer, match::expr::FormatParseError(*parsed.error));
        return std::nullopt;
    }
    match::expr::Schema schema;
    for (const auto& [name, value] : settings) {
        schema.settings[name] = match::expr::Type::kInt;
    }
    const match::expr::CheckResult checked = match::expr::CheckParsedExpression(
        *parsed.root, schema, match::expr::Type::kInt, deck.file, pointer,
        match::expr::GuardState{});
    for (const Diagnostic& diagnostic : checked.diagnostics) {
        AddDiagnostic(diagnostics, diagnostic);
    }
    if (!checked.diagnostics.empty()) {
        return std::nullopt;
    }

    SettingsContext context(settings);
    const match::expr::EvalResult evaluated = match::expr::Evaluate(*parsed.root, context);
    if (const auto* error = std::get_if<match::expr::EvalError>(&evaluated)) {
        AddError(diagnostics, deck.file, entry.pointer,
                 "copies expression \"" + source + "\" failed: " + error->code + ": " +
                     error->message);
        return std::nullopt;
    }
    const int64_t* count = std::get_if<int64_t>(&std::get<match::expr::Value>(evaluated));
    if (count == nullptr) {
        AddError(diagnostics, deck.file, entry.pointer,
                 "copies expression \"" + source + "\" is not an int");
        return std::nullopt;
    }
    if (*count < 0) {
        AddError(diagnostics, deck.file, entry.pointer,
                 "copies expression \"" + source + "\" is " + std::to_string(*count) +
                     "; the count must not be negative");
        return std::nullopt;
    }
    if (*count > static_cast<int64_t>(kMaximumGeneratedKinds)) {
        AddError(diagnostics, deck.file, entry.pointer,
                 "copies expression \"" + source + "\" is " + std::to_string(*count) +
                     "; the count must not exceed " + std::to_string(kMaximumGeneratedKinds));
        return std::nullopt;
    }
    return static_cast<int>(*count);
}

}  // namespace

CatalogResult ExpandCards(const std::vector<ModSchema>& mods) {
    CatalogResult result;

    // INFO: the member total of an attribute is counted over every mod first, so
    // an attribute over the cap is dropped whole and the report does not depend
    // on which member crossed the cap.
    std::map<std::string, std::size_t> attribute_totals;
    for (const ModSchema& mod : mods) {
        for (const MemberDef& member : mod.members) {
            ++attribute_totals[member.attribute];
        }
    }

    // INFO: members are indexed by attribute across every mod in load order and
    // sorted by `order`, then identifier. A slot resolves its member through the
    // pair (attribute, local name), so two members of different attributes that
    // share a local name do not overwrite each other. Two members of ONE
    // attribute that share a local name are a collision and are reported naming
    // both identifiers; the check is local here, not CheckIdentifierCollisions,
    // because that helper compares generated card ids rather than member names.
    std::map<std::string, std::vector<const MemberDef*>> members_by_attribute;
    std::map<std::pair<std::string, std::string>, const MemberDef*> first_member_by_local;
    std::set<std::string> over_cap_attributes;
    for (const ModSchema& mod : mods) {
        for (const MemberDef& member : mod.members) {
            const std::size_t total = attribute_totals[member.attribute];
            if (total > kMaximumMembersPerAttribute) {
                // INFO: one diagnostic names the attribute and its count, at the
                // first member's file; the attribute is left out of the index so
                // no partial cross product is built from it.
                if (over_cap_attributes.insert(member.attribute).second) {
                    AddError(result.diagnostics, member.file,
                             std::string(match::expr::kRootPointer),
                             "attribute \"" + member.attribute + "\" has " +
                                 std::to_string(total) + " members; the maximum is " +
                                 std::to_string(kMaximumMembersPerAttribute));
                }
                continue;
            }
            const std::string local = MemberLocal(member);
            const auto insertion =
                first_member_by_local.emplace(std::make_pair(member.attribute, local), &member);
            if (!insertion.second) {
                AddError(result.diagnostics, member.file, std::string(match::expr::kRootPointer),
                         "member \"" + member.identifier + "\" has the same local name \"" +
                             local + "\" as \"" + insertion.first->second->identifier + "\" in " +
                             insertion.first->second->file);
            }
            members_by_attribute[member.attribute].push_back(&member);
        }
    }
    for (auto& entry : members_by_attribute) {
        std::sort(entry.second.begin(), entry.second.end(),
                  [](const MemberDef* left, const MemberDef* right) {
                      if (left->order != right->order) {
                          return left->order < right->order;
                      }
                      return left->identifier < right->identifier;
                  });
    }

    std::set<std::string> known_attributes;
    for (const ModSchema& mod : mods) {
        for (const AttributeDef& attribute : mod.attributes) {
            known_attributes.insert(attribute.identifier);
        }
    }
    const std::vector<std::string> attribute_candidates(known_attributes.begin(),
                                                        known_attributes.end());

    std::map<std::string, const KindTemplate*> kinds_by_id;
    for (const ModSchema& mod : mods) {
        for (const KindTemplate& kind : mod.kinds) {
            kinds_by_id.emplace(kind.identifier, &kind);
        }
    }

    std::map<std::string, std::size_t> host_counts;
    std::map<std::string, std::size_t> first_source;
    std::vector<std::string> member_descriptions;
    std::vector<std::string> member_files;

    for (const ModSchema& mod : mods) {
        for (const KindTemplate& kind : mod.kinds) {
            if (kind.is_abstract) {
                continue;
            }

            // INFO: `extends` is validated for every concrete template, so a
            // missing parent or a cycle is reported; the chain is not applied to
            // the expansion (see the header comment of catalog.hpp).
            if (BuildExtendsChain(kind, kinds_by_id, result.diagnostics).empty()) {
                continue;
            }

            bool all_attributes_known = true;
            for (std::size_t index = 0; index < kind.slots.size(); ++index) {
                const std::string& attribute = kind.slots[index].attribute;
                if (known_attributes.count(attribute) != 0) {
                    continue;
                }
                AddError(result.diagnostics, kind.file, AttributePointerFor(kind, index),
                         "unknown attribute \"" + attribute + "\"",
                         match::expr::SuggestClosest(attribute, attribute_candidates));
                all_attributes_known = false;
            }
            if (!all_attributes_known) {
                continue;
            }

            if (!CheckFormatSyntax(kind, result.diagnostics)) {
                continue;
            }

            const std::vector<std::string> slot_order = EffectiveSlotOrder(kind);
            std::vector<std::vector<const MemberDef*>> member_lists;
            std::map<std::string, std::size_t> member_counts;
            member_lists.reserve(slot_order.size());
            for (const std::string& slot : slot_order) {
                const SlotBinding* binding = FindBinding(kind, slot);
                const auto found = binding == nullptr
                                       ? members_by_attribute.end()
                                       : members_by_attribute.find(binding->attribute);
                if (found == members_by_attribute.end() || found->second.empty()) {
                    member_lists.emplace_back();
                    member_counts[slot] = 0;
                    continue;
                }
                member_lists.push_back(found->second);
                member_counts[slot] = found->second.size();
            }

            const std::string host_mod = NamespaceOf(kind.identifier);
            const std::optional<std::uint64_t> collapsed =
                CollapsedProduct(kind, slot_order, member_counts);

            // INFO: the collapsed size can be zero when a slot has no member; a
            // template with no card cannot exceed the cap, so it is skipped here.
            if (collapsed && *collapsed == 0) {
                continue;
            }

            const std::size_t host_count = host_counts[host_mod];
            bool over_cap = !collapsed;
            std::uint64_t collapsed_count = 0;
            if (collapsed) {
                collapsed_count = *collapsed;
                if (collapsed_count > kMaximumGeneratedKinds ||
                    host_count > kMaximumGeneratedKinds - collapsed_count) {
                    over_cap = true;
                }
            }
            if (over_cap) {
                const std::string amount =
                    collapsed ? std::to_string(collapsed_count)
                              : "more than " + std::to_string(kMaximumGeneratedKinds);
                AddError(result.diagnostics, kind.file, IdentifierPointer(kind),
                         "template \"" + kind.identifier + "\" expands to " + amount +
                             " cards; host mod \"" + host_mod + "\" exceeds " +
                             std::to_string(kMaximumGeneratedKinds) + " kinds");
                // INFO: a rejected template still consumes the host budget, so
                // the host total counts every declared template and a template
                // that pushes the host over the cap cannot be "refunded" by
                // rejecting it.
                host_counts[host_mod] += collapsed ? collapsed_count : kOverBudgetCount;
                continue;
            }

            std::set<std::string> seen_canonical;
            ForEachAssignment(member_lists, [&](const std::vector<const MemberDef*>& members) {
                std::map<std::string, std::string> slot_values;
                for (std::size_t index = 0; index < slot_order.size(); ++index) {
                    slot_values[slot_order[index]] = MemberLocal(*members[index]);
                }

                const std::map<std::string, std::string> canonical =
                    CanonicalizeSymmetric(kind, slot_values);

                // INFO: the dedupe key names the member each canonical slot
                // resolves to, not the local name alone, so two members of one
                // local name in different attributes do not collapse into one
                // another.
                std::string canonical_key;
                for (const std::string& slot : slot_order) {
                    const MemberDef* member = ResolveSlotMember(kind, slot, canonical,
                                                                first_member_by_local);
                    if (member == nullptr) {
                        continue;
                    }
                    canonical_key += slot;
                    canonical_key += '=';
                    canonical_key += member->attribute;
                    canonical_key += ':';
                    canonical_key += member->identifier;
                    canonical_key += ';';
                }
                if (!seen_canonical.insert(canonical_key).second) {
                    return;
                }

                const std::optional<std::string> kind_id =
                    GenerateIdentifier(kind, canonical, result.diagnostics);
                if (!kind_id) {
                    return;
                }
                std::optional<GeneratedCard> card =
                    BuildCard(kind, *kind_id, slot_order, canonical, first_member_by_local,
                              result.diagnostics);
                if (!card) {
                    return;
                }

                const auto found = first_source.find(card->kind_id);
                if (found == first_source.end()) {
                    first_source.emplace(card->kind_id, result.cards.size());
                    member_descriptions.push_back(DescribeMembers(slot_order, canonical));
                    member_files.push_back(kind.file);
                    host_counts[card->host_mod] += 1;
                    result.cards.push_back(std::move(*card));
                    return;
                }
                // INFO: this pass keys ids in a map, like CheckIdentifierCollisions,
                // but the brief requires the diagnostic to name both templates,
                // their member values and the file of the first one, which that
                // helper's message does not carry; it also spans every mod.
                AddError(result.diagnostics, kind.file, IdentifierPointer(kind),
                         "card \"" + card->kind_id + "\" is generated by both \"" +
                             result.cards[found->second].template_id + "\" (" +
                             member_descriptions[found->second] + ") in " +
                             member_files[found->second] + " and \"" + card->template_id + "\" (" +
                             DescribeMembers(slot_order, canonical) + ")");
            });
        }
    }

    DedupeDiagnostics(result.diagnostics);
    std::sort(result.cards.begin(), result.cards.end(),
              [](const GeneratedCard& left, const GeneratedCard& right) {
                  return left.kind_id < right.kind_id;
              });
    return result;
}

ExpandedDeck ExpandDeck(const std::vector<ModSchema>& mods, const std::string& deck_id,
                        const std::set<std::string>& active_mods,
                        const std::map<std::string, int64_t>& settings) {
    ExpandedDeck result;
    result.deck_id = deck_id;

    const DeckSpec* deck = FindDeck(mods, deck_id);
    if (deck == nullptr) {
        AddError(result.diagnostics, "", std::string(match::expr::kRootPointer),
                 "unknown deck \"" + deck_id + "\"");
        return result;
    }
    result.name = deck->name;
    result.mods = deck->mods;

    // INFO: the active mods are the mods whose members and templates the deck
    // may use; an empty active set means every loaded mod. The member index
    // points into `mods`, whose elements are stable for the whole call.
    std::vector<ModSchema> active_schemas;
    std::map<std::string, std::vector<const MemberDef*>> members_by_attribute;
    for (const ModSchema& mod : mods) {
        if (!active_mods.empty() && active_mods.count(mod.mod_id) == 0) {
            continue;
        }
        active_schemas.push_back(mod);
        for (const MemberDef& member : mod.members) {
            members_by_attribute[member.attribute].push_back(&member);
        }
    }
    for (auto& [attribute, members] : members_by_attribute) {
        std::sort(members.begin(), members.end(), CompareMembers);
    }

    // INFO: the deck may only deal cards the active mods generate, so the
    // catalog of the active mods is the id set every expanded id is checked
    // against. Its diagnostics are the caller's business, not the deck's.
    const CatalogResult active_catalog = ExpandCards(active_schemas);
    std::set<std::string> active_kind_ids;
    for (const GeneratedCard& card : active_catalog.cards) {
        active_kind_ids.insert(card.kind_id);
    }

    std::map<std::string, const KindTemplate*> kinds_by_id;
    for (const ModSchema& mod : active_schemas) {
        for (const KindTemplate& kind : mod.kinds) {
            kinds_by_id.emplace(kind.identifier, &kind);
        }
    }

    const int requested_limit = std::min(deck->max_cards, kMaximumDeckCards);
    const std::uint64_t limit =
        requested_limit > 0 ? static_cast<std::uint64_t>(requested_limit) : 0;
    std::uint64_t total = 0;
    std::map<std::string, std::size_t> position;

    for (const DeckEntry& entry : deck->cards) {
        const auto kind_found = kinds_by_id.find(entry.kind);
        if (kind_found == kinds_by_id.end()) {
            std::vector<std::string> candidates;
            candidates.reserve(kinds_by_id.size());
            for (const auto& [id, kind] : kinds_by_id) {
                candidates.push_back(id);
            }
            AddError(result.diagnostics, deck->file, entry.pointer,
                     "unknown kind \"" + entry.kind + "\"",
                     match::expr::SuggestClosest(entry.kind, candidates));
            continue;
        }
        const KindTemplate& kind = *kind_found->second;
        if (kind.is_abstract) {
            AddError(result.diagnostics, deck->file, entry.pointer,
                     "kind \"" + entry.kind + "\" is abstract");
            continue;
        }

        const std::vector<std::string> slot_order = EffectiveSlotOrder(kind);
        std::vector<std::vector<const MemberDef*>> member_lists;
        member_lists.reserve(slot_order.size());
        bool selectors_ok = true;
        for (const std::string& slot : slot_order) {
            const SlotBinding* binding = FindBinding(kind, slot);
            std::vector<const MemberDef*> members;
            if (binding != nullptr) {
                const auto found = members_by_attribute.find(binding->attribute);
                if (found != members_by_attribute.end()) {
                    members = found->second;
                }
            }
            std::vector<const MemberDef*> selected;
            const auto selector = entry.members.find(slot);
            if (selector == entry.members.end()) {
                selected = members;
            } else if (!SelectMembers(selector->second,
                                      binding == nullptr ? std::string() : binding->attribute,
                                      members, deck->file, result.diagnostics, selected)) {
                selectors_ok = false;
            }
            member_lists.push_back(std::move(selected));
        }
        if (!selectors_ok) {
            continue;
        }

        const std::optional<std::uint64_t> combinations =
            ResolveCombinationCount(entry.combine, member_lists, deck->file, entry.pointer,
                                    result.diagnostics);
        if (!combinations) {
            continue;
        }
        const std::optional<int> copies = ResolveCopies(entry, *deck, settings, result.diagnostics);
        if (!copies) {
            continue;
        }

        const std::optional<std::uint64_t> contribution =
            MultiplyCounts(*combinations, static_cast<std::uint64_t>(*copies));
        if (!contribution || *contribution > limit - total) {
            const std::string amount =
                contribution ? std::to_string(total + *contribution)
                             : "more than " + std::to_string(limit);
            AddError(result.diagnostics, deck->file, entry.pointer,
                     "deck \"" + deck->identifier + "\" has " + amount + " cards, the limit is " +
                         std::to_string(limit));
            result.total = static_cast<int>(total);
            DedupeDiagnostics(result.diagnostics);
            return result;
        }
        total += *contribution;

        ForEachCombination(entry.combine, member_lists,
                           [&](const std::vector<const MemberDef*>& members) {
                               std::map<std::string, std::string> slot_values;
                               for (std::size_t index = 0; index < slot_order.size(); ++index) {
                                   slot_values[slot_order[index]] = MemberLocal(*members[index]);
                               }
                               const std::optional<std::string> kind_id =
                                   GenerateIdentifier(kind, slot_values, result.diagnostics);
                               if (!kind_id) {
                                   return;
                               }
                               // INFO: an id the active catalog does not hold would be a
                               // card the deck deals but the match cannot build; the
                               // diagnostic belongs to the deck entry that asked for it.
                               if (active_kind_ids.count(*kind_id) == 0) {
                                   AddError(result.diagnostics, deck->file, entry.pointer,
                                            "card \"" + *kind_id +
                                                "\" is not generated by the active mods");
                                   return;
                               }
                               const auto found = position.find(*kind_id);
                               if (found == position.end()) {
                                   position.emplace(*kind_id, result.cards.size());
                                   result.cards.emplace_back(*kind_id, *copies);
                                   return;
                               }
                               result.cards[found->second].second += *copies;
                           });
    }

    result.total = static_cast<int>(total);
    DedupeDiagnostics(result.diagnostics);
    return result;
}

}  // namespace match::cardschema

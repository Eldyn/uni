#include <match/cardschema/identifier.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <match/expr/diagnostic.hpp>

namespace match::cardschema {

namespace {

void AddError(std::vector<Diagnostic>& diagnostics, const std::string& file,
              const std::string& pointer, std::string message, std::string suggestion = {}) {
    diagnostics.push_back(
        Diagnostic{file, pointer, std::move(message), std::move(suggestion)});
}

std::string KindPointer(const char* key) {
    return match::expr::AppendPointerToken(
        match::expr::AppendPointerToken(match::expr::kRootPointer, "kind"), key);
}

std::string CapitalizeFirst(std::string value) {
    if (!value.empty() && value.front() >= 'a' && value.front() <= 'z') {
        value.front() = static_cast<char>(value.front() - 'a' + 'A');
    }
    return value;
}

// INFO: fills one format; `capitalize` upper cases the first letter of every
// substituted value, which the transitional title form uses. A placeholder is
// looked up in `slot_values`; the candidates of the suggestion are the slot
// names the caller passed.
std::optional<std::string> FillFormat(const std::string& format,
                                      const std::map<std::string, std::string>& slot_values,
                                      const std::string& file, const std::string& pointer,
                                      bool capitalize, std::vector<Diagnostic>& diagnostics) {
    std::vector<std::string> candidates;
    candidates.reserve(slot_values.size());
    for (const auto& entry : slot_values) {
        candidates.push_back(entry.first);
    }

    const std::size_t initial_diagnostics = diagnostics.size();
    std::string filled;
    filled.reserve(format.size());
    std::size_t index = 0;
    while (index < format.size()) {
        const char character = format[index];
        if (character != '{') {
            filled += character;
            ++index;
            continue;
        }
        const std::size_t end = format.find('}', index + 1);
        if (end == std::string::npos) {
            AddError(diagnostics, file, pointer, "unclosed placeholder \"{\"");
            return std::nullopt;
        }
        const std::string name = format.substr(index + 1, end - index - 1);
        const auto found = slot_values.find(name);
        if (found == slot_values.end()) {
            AddError(diagnostics, file, pointer, "unknown placeholder \"{" + name + "}\"",
                     match::expr::SuggestClosest(name, candidates));
        } else {
            filled += capitalize ? CapitalizeFirst(found->second) : found->second;
        }
        index = end + 1;
    }
    if (diagnostics.size() != initial_diagnostics) {
        return std::nullopt;
    }
    return filled;
}

// INFO: the order the default format uses: the kind's `slot_order` first, then
// the declared attribute bindings, then any remaining slot of the assignment, so
// a slot the kind does not declare is not dropped from the identifier.
std::vector<std::string> OrderedSlotNames(const KindTemplate& kind,
                                          const std::map<std::string, std::string>& slot_values) {
    std::vector<std::string> order;
    order.reserve(slot_values.size());
    const auto append = [&order, &slot_values](const std::string& name) {
        if (slot_values.count(name) == 0 ||
            std::find(order.begin(), order.end(), name) != order.end()) {
            return;
        }
        order.push_back(name);
    };
    for (const std::string& slot : kind.slot_order) {
        append(slot);
    }
    for (const SlotBinding& binding : kind.slots) {
        append(binding.name);
    }
    for (const auto& entry : slot_values) {
        append(entry.first);
    }
    return order;
}

}  // namespace

std::map<std::string, std::string> CanonicalizeSymmetric(
    const KindTemplate& kind, const std::map<std::string, std::string>& slot_members) {
    std::map<std::string, std::string> canonical = slot_members;
    for (const std::vector<std::string>& group : kind.symmetric_groups) {
        std::vector<std::string> members;
        members.reserve(group.size());
        for (const std::string& slot : group) {
            const auto found = canonical.find(slot);
            if (found != canonical.end()) {
                members.push_back(found->second);
            }
        }
        std::sort(members.begin(), members.end());
        std::size_t next_member_index = 0;
        for (const std::string& slot : group) {
            if (canonical.find(slot) == canonical.end()) {
                continue;
            }
            canonical[slot] = members[next_member_index];
            ++next_member_index;
        }
    }
    return canonical;
}

bool SameCard(const KindTemplate& kind, const std::map<std::string, std::string>& a,
              const std::map<std::string, std::string>& b) {
    return CanonicalizeSymmetric(kind, a) == CanonicalizeSymmetric(kind, b);
}

std::optional<std::string> FillTemplate(const std::string& format,
                                        const std::map<std::string, std::string>& slot_values,
                                        const std::string& file, const std::string& pointer,
                                        std::vector<Diagnostic>& diagnostics) {
    return FillFormat(format, slot_values, file, pointer, false, diagnostics);
}

std::optional<std::string> FillTitle(const std::string& format,
                                     const std::map<std::string, std::string>& slot_values,
                                     const std::string& file, const std::string& pointer,
                                     std::vector<Diagnostic>& diagnostics) {
    return FillFormat(format, slot_values, file, pointer, true, diagnostics);
}

std::optional<std::string> GenerateIdentifier(const KindTemplate& kind,
                                              const std::map<std::string, std::string>& slot_values,
                                              std::vector<Diagnostic>& diagnostics) {
    const std::map<std::string, std::string> canonical =
        CanonicalizeSymmetric(kind, slot_values);
    std::string format = kind.identifier_format;
    std::string pointer = KindPointer("identifier_format");
    if (format.empty()) {
        format = LocalNameOf(kind.identifier);
        for (const std::string& slot : OrderedSlotNames(kind, canonical)) {
            format += "_{" + slot + "}";
        }
        pointer = KindPointer("identifier");
    }

    const std::optional<std::string> filled =
        FillFormat(format, canonical, kind.file, pointer, false, diagnostics);
    if (!filled) {
        return std::nullopt;
    }

    const std::string identifier = NamespaceOf(kind.identifier) + ":" + *filled;
    if (!IsValidLocalName(*filled)) {
        AddError(diagnostics, kind.file, pointer,
                 "card identifier \"" + identifier +
                     "\" is not a valid identifier; a local name uses [a-z0-9_] "
                     "(1 to 32 characters)");
        return std::nullopt;
    }
    return identifier;
}

}  // namespace match::cardschema

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <match/cardschema/model.hpp>

namespace match::cardschema {

// INFO: `slot_members` maps a slot name to its member LOCAL name, never a full
// identifier, so the sort key is the plain byte order of the names the caller
// passes. Within each symmetric group of `kind` the present member names are
// sorted and assigned back to the group's slots in declared order; every other
// slot is left as it came in. A group reads the map as it evolves, so a slot in
// two groups is written by the earlier group before the later one reads it;
// groups should be disjoint and the loader rejects overlapping ones. A partial
// assignment is canonicalized over the slots it holds only, so callers pass
// complete assignments.
std::map<std::string, std::string> CanonicalizeSymmetric(
    const KindTemplate& kind, const std::map<std::string, std::string>& slot_members);

// INFO: true when two assignments describe one card (the same canonical form).
bool SameCard(const KindTemplate& kind, const std::map<std::string, std::string>& a,
              const std::map<std::string, std::string>& b);

// INFO: fills the "{slot}" placeholders of `format` with the member local names
// in `slot_values`; every other character is copied verbatim. A syntax error and
// an unknown placeholder each add a diagnostic against `file` and `pointer` (the
// pointer of `format`) and make the result nullopt; the syntax error is the only
// diagnostic, while every unknown placeholder is still checked.
std::optional<std::string> FillTemplate(const std::string& format,
                                        const std::map<std::string, std::string>& slot_values,
                                        const std::string& file, const std::string& pointer,
                                        std::vector<Diagnostic>& diagnostics);

// INFO: FillTemplate with the first letter of every substituted value upper
// cased; the transitional card title and face text use it.
std::optional<std::string> FillTitle(const std::string& format,
                                     const std::map<std::string, std::string>& slot_values,
                                     const std::string& file, const std::string& pointer,
                                     std::vector<Diagnostic>& diagnostics);

// INFO: the card identifier "<kind namespace>:<local>", where the local part is
// `kind.identifier_format` filled with the canonical slot values when the format
// is set, and otherwise the kind's local name followed by the value of every
// slot in declared order, joined by "_". Symmetric groups are canonicalized
// first. A local part outside [a-z0-9_]{1,32} adds a diagnostic against
// `kind.file` and makes the result nullopt; the pointer is that of the format
// that produced the local part.
std::optional<std::string> GenerateIdentifier(const KindTemplate& kind,
                                              const std::map<std::string, std::string>& slot_values,
                                              std::vector<Diagnostic>& diagnostics);

// INFO: checks the placeholder grammar of `format` without filling it: a
// placeholder is "{" name "}". Returns nullopt when `format` is well formed and
// otherwise the message of the first syntax error, one of `empty placeholder`,
// `nested "{" in placeholder`, `unmatched "}"` and `unclosed "{"`. The loader
// reports the message at the field pointer and `FillTemplate` adds it as a
// diagnostic.
std::optional<std::string> ValidateFormatSyntax(std::string_view format);

// INFO: the placeholder names of a well-formed `format`, in order of appearance.
// A caller that needs the names checks ValidateFormatSyntax first; on a malformed
// format the scan stops at the error and returns the names read so far.
std::vector<std::string> FormatPlaceholderNames(std::string_view format);

// INFO: one generated card id and the place it was generated from, so a
// diagnostic can name both sources of a collision.
struct GeneratedIdentifier {
    std::string id;
    std::string file;
    std::string pointer;
};

// INFO: one diagnostic per id generated more than once, at the second source's
// file and pointer, naming both sources. The lookup is a map from id to the
// index of its first source, so the pass is linear in the input size; at most
// the first 100 collisions are reported.
std::vector<Diagnostic> CheckIdentifierCollisions(
    const std::vector<GeneratedIdentifier>& generated);

}  // namespace match::cardschema

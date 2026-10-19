#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <match/cardschema/model.hpp>

// INFO: helpers shared by the loader, the catalog expansion and the catalog
// validation. They live in one header so the three report errors the same way:
// the same diagnostic cap and the same one-line truncation marker.
namespace match::cardschema::internal {

// INFO: a hopeless input can produce thousands of errors; after this many a
// caller emits one final marker instead, so a bad mod cannot flood the report.
inline constexpr std::size_t kMaximumDiagnostics = 100;

// INFO: the message of the single diagnostic that ends a capped report.
inline constexpr const char* kTruncationMarker = "more errors not shown";

// INFO: appends one error, capped at kMaximumDiagnostics. The first call past
// the cap adds the truncation marker once; every later call is dropped.
inline void AddError(std::vector<Diagnostic>& diagnostics, const std::string& file,
                     const std::string& pointer, std::string message,
                     std::string suggestion = {}) {
    if (diagnostics.size() >= kMaximumDiagnostics) {
        if (diagnostics.size() == kMaximumDiagnostics) {
            diagnostics.push_back(Diagnostic{file, pointer, kTruncationMarker, {}});
        }
        return;
    }
    diagnostics.push_back(Diagnostic{file, pointer, std::move(message), std::move(suggestion)});
}

// INFO: the name a member contributes to an identifier and a title: its alias
// when it sets one, otherwise the local part of its identifier.
inline std::string MemberLocal(const MemberDef& member) {
    if (!member.identifier_alias.empty()) {
        return member.identifier_alias;
    }
    return LocalNameOf(member.identifier);
}

}  // namespace match::cardschema::internal

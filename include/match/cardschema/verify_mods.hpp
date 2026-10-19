#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace match::cardschema {

// INFO: runs the `uni-mods verify` command for a mods folder. `args` starts with
// the command name.
//   verify --mods-root <dir> [--mod <id>] [--deck <identifier>]
// Every direct subfolder of the root that holds a `mod.json` is one mod: the
// manifest `id` names it, or the folder name when the manifest has none. Each
// mod is loaded, the whole catalog is expanded and validated, and every deck is
// expanded with the manifest settings (each `int` setting's `id` and `default`).
//
// INFO: `--mod` restricts which mods are reported and which decks are expanded
// (all mods still load). A diagnostic is reported when its label does not name a
// mod folder (`mods/...`), when the label names the selected mod, or when the
// selected mod is involved in a cross-mod problem: the message or the suggestion
// carries the mod's identifier namespace (`<mod>:`). `--deck` expands only the
// deck with that identifier of the reported mods.
//
// INFO: the exit codes are:
//   0  clean; prints `ok`, then one `deck <id>: <total> cards` line per deck.
//   1  diagnostics; prints every formatted diagnostic, one per line, then one
//      `more diagnostics not shown` line when the report is capped.
//   2  usage error; prints the message when there is one, then the usage line.
//   3  IO error; prints `cannot read mods root "<dir>": <reason>` or a manifest
//      read failure, without a usage line.
int RunVerifyMods(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

// INFO: the entry point of the `uni-mods verify` tool. Only the flag-bearing
// positions of `args` select the verifier: the first argument and every second
// one after it. `--mods-root` runs the mods-folder verifier, `--schema` runs the
// phase-1 expression verifier, and the two together are a usage error.
int RunVerifyCommand(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace match::cardschema

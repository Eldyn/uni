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
// `--mod` restricts which mods are reported (all still load); `--deck` expands
// only the deck with that identifier. The exit codes are:
//   0  clean; prints `ok`, then one `deck <id>: <total> cards` line per deck.
//   1  diagnostics; prints every formatted diagnostic, one per line.
//   2  usage error; prints the message when there is one, then the usage line.
int RunVerifyMods(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace match::cardschema

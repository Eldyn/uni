#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace match::expr {

// INFO: runs the `uni-mods verify` command. `args` starts with the command
// name. The exit codes are:
//   0  the expression is clean; prints `ok`.
//   1  diagnostics: a bad expression, an expression over the 65536-byte limit,
//      or a schema with an unknown key or type; prints every formatted
//      diagnostic.
//   2  usage error: an unknown command, an unknown flag, a missing value, a
//      repeated flag, an unknown `--expect` type, or a missing required flag;
//      prints the message and the usage line.
//   3  IO error: an unreadable file, a path that is not a regular file, a file
//      over the 1 MiB limit, invalid JSON, or an internal error.
int RunVerify(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace match::expr

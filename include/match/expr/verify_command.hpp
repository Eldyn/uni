#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace match::expr {

// INFO: runs the `uni-mods verify` command. `args` starts with the command
// name. It returns 0 and prints `ok` for a clean expression, 1 and prints every
// formatted diagnostic for a bad one, and 2 and prints the usage for bad
// arguments.
int RunVerify(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace match::expr

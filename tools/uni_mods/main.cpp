#include <match/expr/verify_command.hpp>

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }
    return match::expr::RunVerify(args, std::cout, std::cerr);
}

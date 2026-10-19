#include <match/expr/verify_command.hpp>

#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        std::vector<std::string> args;
        args.reserve(static_cast<std::size_t>(argc));
        for (int index = 1; index < argc; ++index) {
            args.emplace_back(argv[index]);
        }
        return match::expr::RunVerify(args, std::cout, std::cerr);
    } catch (const std::exception& error) {
        std::cerr << "internal error: " << error.what() << "\n";
        return 3;
    }
}

#include "llvp/app/config.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

llvp::Config parse(std::vector<std::string> arguments) {
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    return llvp::parse_config(static_cast<int>(pointers.size()), pointers.data());
}

int main() {
    const std::vector<std::string> required{
        "detector", "--source", "camera:1", "--model", "model.pt", "--ops", "ops.so"};
    auto arguments = required;
    arguments.insert(arguments.end(), {"--fifo", "--display", "--duration-seconds", "2.5"});
    const auto config = parse(arguments);
    if (config.latest || !config.display || config.duration_seconds != 2.5) return 1;
    if (!parse(required).latest) return 1;

    for (const auto& invalid : {"0", "-1", "nan", "inf", "1second", ""}) {
        arguments = required;
        arguments.insert(arguments.end(), {"--duration-seconds", invalid});
        try {
            parse(arguments);
            std::cerr << "accepted invalid duration: " << invalid << '\n';
            return 1;
        } catch (const std::invalid_argument&) {
            // Expected: bad input is rejected before opening a camera.
        }
    }
    for (const auto& option : {"--unknown", "--source"}) {
        arguments = required;
        arguments.push_back(option);
        try {
            parse(arguments);
            return 1;
        } catch (const std::invalid_argument&) {
        }
    }
    std::cout << "All configuration tests passed\n";
}

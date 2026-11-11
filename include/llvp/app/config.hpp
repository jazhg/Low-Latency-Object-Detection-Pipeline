#pragma once

#include <string>

namespace llvp {
// Options shared by capture, inference, and the optional display.
struct Config {
    std::string source;
    std::string model;
    std::string ops;
    bool latest{true};
    bool display{false};
    double duration_seconds{0.0};  // Zero means run until the source ends or q is pressed.
};

Config parse_config(int argc, char** argv);
}  // namespace llvp

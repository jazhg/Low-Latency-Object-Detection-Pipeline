#include "llvp/app/config.hpp"

#include <cmath>
#include <stdexcept>

namespace llvp {
namespace {
// Reject partial numbers such as "30seconds" rather than silently accepting 30.
double parse_duration(const std::string& text) {
    std::size_t used = 0;
    const double seconds = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(seconds) || seconds <= 0.0) {
        throw std::invalid_argument("duration must be a positive number: " + text);
    }
    return seconds;
}

std::string next_value(int& index, int argc, char** argv) {
    const std::string option(argv[index]);
    if (++index >= argc) {
        throw std::invalid_argument(option + " requires a value");
    }
    return argv[index];
}
}  // namespace

Config parse_config(int argc, char** argv) {
    // Start with defaults, then consume each option and its value (if it needs one).
    Config result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--source") {
            result.source = next_value(i, argc, argv);
        } else if (arg == "--model") {
            result.model = next_value(i, argc, argv);
        } else if (arg == "--ops") {
            result.ops = next_value(i, argc, argv);
        } else if (arg == "--fifo") {
            result.latest = false;
        } else if (arg == "--latest") {
            result.latest = true;
        } else if (arg == "--display") {
            result.display = true;
        } else if (arg == "--duration-seconds") {
            result.duration_seconds = parse_duration(next_value(i, argc, argv));
        } else {
            throw std::invalid_argument("unknown argument: " + arg);
        }
    }
    // Validate before creating threads or opening a camera.
    if (result.source.empty() || result.model.empty() || result.ops.empty()) {
        throw std::invalid_argument(
            "usage: low_latency_detector --source VIDEO --model MODEL --ops "
            "TORCHVISION_OPS [--fifo|--latest] [--display] "
            "[--duration-seconds SECONDS]");
    }
    return result;
}
}  // namespace llvp

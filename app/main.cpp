#include "llvp/app/config.hpp"
#include "llvp/pipeline/video_pipeline.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        // Keep setup and runtime errors on the same user-facing reporting path.
        const auto config = llvp::parse_config(argc, argv);
        return llvp::run_video_pipeline(config);
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

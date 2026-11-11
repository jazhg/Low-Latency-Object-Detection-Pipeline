#pragma once

#include "llvp/app/config.hpp"

namespace llvp {
// Starts capture and inference workers, handles display, then reports results.
int run_video_pipeline(const Config& config);
}  // namespace llvp

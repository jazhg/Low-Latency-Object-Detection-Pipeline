#pragma once

#include <cstdint>
#include <vector>

namespace llvp {
struct Detection {
    float x1{};
    float y1{};
    float x2{};
    float y2{};
    float score{};
    std::int64_t class_id{};
};

using Detections = std::vector<Detection>;
}  // namespace llvp

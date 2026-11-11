#pragma once

#include "llvp/core/detection.hpp"
#include <opencv2/core/mat.hpp>

namespace llvp {
// Return an annotated copy; never modify the captured frame.
cv::Mat draw_detections(const cv::Mat& image, const Detections& detections);
}  // namespace llvp

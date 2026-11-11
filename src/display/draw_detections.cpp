#include "llvp/display/draw_detections.hpp"
#include <opencv2/imgproc.hpp>

namespace llvp {
cv::Mat draw_detections(const cv::Mat& image, const Detections& detections) {
    cv::Mat annotated = image.clone();
    // The exported model returns coordinates in its 320-by-320 input image.
    const float scale_x = static_cast<float>(image.cols) / 320.0F;
    const float scale_y = static_cast<float>(image.rows) / 320.0F;
    for (const auto& detection : detections) {
        const cv::Point top_left(static_cast<int>(detection.x1 * scale_x),
                                 static_cast<int>(detection.y1 * scale_y));
        const cv::Point bottom_right(static_cast<int>(detection.x2 * scale_x),
                                     static_cast<int>(detection.y2 * scale_y));
        cv::rectangle(annotated, top_left, bottom_right, cv::Scalar(0, 255, 0), 2);
    }
    return annotated;
}
}  // namespace llvp

#pragma once

#include "llvp/core/detection.hpp"

#include <opencv2/core/mat.hpp>

#include <memory>
#include <string>

namespace llvp {
class TorchDetector {
public:
    TorchDetector(const std::string& model_path,
                  const std::string& torchvision_ops_path);
    ~TorchDetector();
    TorchDetector(TorchDetector&&) noexcept;
    TorchDetector& operator=(TorchDetector&&) noexcept;
    TorchDetector(const TorchDetector&) = delete;
    TorchDetector& operator=(const TorchDetector&) = delete;

    [[nodiscard]] Detections detect(const cv::Mat& bgr_image);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace llvp

#include "llvp/inference/torch_detector.hpp"

#include <ATen/Parallel.h>
#include <torch/script.h>

#include <opencv2/imgproc.hpp>

#include <dlfcn.h>
#include <stdexcept>
#include <utility>

namespace llvp {
class TorchDetector::Impl {
public:
    Impl(const std::string& model_path, const std::string& ops_path) {
        // Register torchvision operators before loading the model that uses them.
        // Keep this library loaded for the process lifetime: Torch retains registrations.
        ops_handle_ = dlopen(ops_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (ops_handle_ == nullptr)
            throw std::runtime_error(std::string("unable to load torchvision ops: ") +
                                     dlerror());
        // Fix CPU parallelism so trials use the same inference configuration.
        at::set_num_threads(1);
        module_ = torch::jit::load(model_path, torch::kCPU);
        module_.eval();
    }

    ~Impl() = default;

    Detections detect(const cv::Mat& bgr_image) {
        if (bgr_image.empty() || bgr_image.type() != CV_8UC3)
            throw std::invalid_argument("detector expects a non-empty CV_8UC3 image");

        // The exported model expects a 320-by-320 RGB image; OpenCV captures BGR.
        cv::Mat resized;
        cv::resize(bgr_image, resized, cv::Size(320, 320), 0.0, 0.0,
                   cv::INTER_LINEAR);
        cv::Mat rgb;
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
        // Change HWC bytes into CHW floats in [0, 1]. Conversion owns the new data.
        torch::Tensor input =
            torch::from_blob(rgb.data, {320, 320, 3}, torch::kUInt8)
                .permute({2, 0, 1})
                .to(torch::kFloat32)
                .div_(255.0)
                .contiguous();

        // Disable gradient tracking because this application only runs predictions.
        torch::InferenceMode inference_mode;
        const torch::Tensor output =
            module_.forward({input}).toTensor().to(torch::kCPU).contiguous();
        // Each output row is x1, y1, x2, y2, score, class_id; NMS is in the model.
        Detections detections;
        detections.reserve(static_cast<std::size_t>(output.size(0)));
        const auto accessor = output.accessor<float, 2>();
        for (std::int64_t row = 0; row < output.size(0); ++row) {
            detections.push_back({accessor[row][0], accessor[row][1],
                                  accessor[row][2], accessor[row][3],
                                  accessor[row][4],
                                  static_cast<std::int64_t>(accessor[row][5])});
        }
        return detections;
    }

private:
    void* ops_handle_{};
    torch::jit::Module module_;
};

TorchDetector::TorchDetector(const std::string& model_path,
                             const std::string& torchvision_ops_path)
    : impl_(std::make_unique<Impl>(model_path, torchvision_ops_path)) {}
TorchDetector::~TorchDetector() = default;
TorchDetector::TorchDetector(TorchDetector&&) noexcept = default;
TorchDetector& TorchDetector::operator=(TorchDetector&&) noexcept = default;
Detections TorchDetector::detect(const cv::Mat& image) {
    return impl_->detect(image);
}
}  // namespace llvp

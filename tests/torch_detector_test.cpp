#include "llvp/inference/torch_detector.hpp"

#include <opencv2/imgcodecs.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#ifndef LLVP_MODEL_PATH
#error LLVP_MODEL_PATH must be defined
#endif
#ifndef LLVP_TORCHVISION_OPS_PATH
#error LLVP_TORCHVISION_OPS_PATH must be defined
#endif
#ifndef LLVP_FIXTURE_IMAGE
#error LLVP_FIXTURE_IMAGE must be defined
#endif

int main() {
    try {
        llvp::TorchDetector detector(LLVP_MODEL_PATH, LLVP_TORCHVISION_OPS_PATH);
        bool rejected_empty = false;
        try {
            static_cast<void>(detector.detect(cv::Mat{}));
        } catch (const std::invalid_argument&) {
            rejected_empty = true;
        }
        if (!rejected_empty) return EXIT_FAILURE;

        const cv::Mat image = cv::imread(LLVP_FIXTURE_IMAGE, cv::IMREAD_COLOR);
        const auto detections = detector.detect(image);
        bool found_dog = false;
        for (const auto& detection : detections)
            found_dog = found_dog ||
                        (detection.class_id == 18 && detection.score >= 0.75F);
        if (!found_dog) {
            std::cerr << "unexpected detector result; count=" << detections.size();
            for (const auto& detection : detections)
                std::cerr << " class=" << detection.class_id
                          << " score=" << detection.score;
            std::cerr << '\n';
            return EXIT_FAILURE;
        }
        std::cout << "Torch detector integration test passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

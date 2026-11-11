#include <torch/script.h>
#include <ATen/Parallel.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <dlfcn.h>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: torch_model_probe MODEL_PATH TORCHVISION_OPS "
                     "FIXTURE_F32\n";
        return EXIT_FAILURE;
    }

    try {
        void* ops = dlopen(argv[2], RTLD_NOW | RTLD_GLOBAL);
        if (ops == nullptr) {
            std::cerr << "unable to load torchvision operators: " << dlerror()
                      << '\n';
            return EXIT_FAILURE;
        }
        torch::InferenceMode inference_mode;
        at::set_num_threads(1);
        torch::jit::Module module = torch::jit::load(argv[1], torch::kCPU);
        module.eval();

        constexpr std::int64_t elements = 3 * 320 * 320;
        torch::Tensor input = torch::empty({3, 320, 320}, torch::kFloat32);
        std::ifstream fixture(argv[3], std::ios::binary);
        fixture.read(reinterpret_cast<char*>(input.data_ptr<float>()),
                     elements * static_cast<std::int64_t>(sizeof(float)));
        if (!fixture || fixture.peek() != std::ifstream::traits_type::eof()) {
            std::cerr << "fixture must contain exactly " << elements
                      << " float32 values\n";
            return EXIT_FAILURE;
        }
        torch::Tensor output = module.forward({input}).toTensor().contiguous();
        if (output.dim() != 2 || output.size(1) != 6) {
            std::cerr << "unexpected output shape: " << output.sizes() << '\n';
            return EXIT_FAILURE;
        }
        if (!torch::isfinite(output).all().item<bool>()) {
            std::cerr << "model output contains non-finite values\n";
            return EXIT_FAILURE;
        }

        std::cout << "rows=" << output.size(0) << '\n';
        std::cout << "sum=" << output.sum().item<double>() << '\n';
        if (output.size(0) > 0) {
            std::cout << "first=" << output[0] << '\n';
        }
        return EXIT_SUCCESS;
    } catch (const c10::Error& error) {
        std::cerr << "LibTorch error: " << error.what_without_backtrace() << '\n';
        return EXIT_FAILURE;
    }
}

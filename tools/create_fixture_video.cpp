#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: create_fixture_video IMAGE OUTPUT FRAME_COUNT\n";
        return EXIT_FAILURE;
    }
    const cv::Mat image = cv::imread(argv[1], cv::IMREAD_COLOR);
    const int count = std::atoi(argv[3]);
    if (image.empty() || count <= 0) return EXIT_FAILURE;
    cv::VideoWriter writer(argv[2], cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                           30.0, image.size());
    if (!writer.isOpened()) return EXIT_FAILURE;
    for (int index = 0; index < count; ++index) writer.write(image);
    return EXIT_SUCCESS;
}

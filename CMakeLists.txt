cmake_minimum_required(VERSION 3.24)

project(low_latency_video_pipeline VERSION 0.1.0 LANGUAGES CXX)

option(LLVP_BUILD_TESTS "Build unit tests" ON)
option(LLVP_BUILD_BENCHMARKS "Build queue benchmarks" ON)
option(LLVP_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UBSanitizer" OFF)
option(LLVP_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(LLVP_BUILD_TORCH_PROBE "Build the LibTorch model feasibility probe" OFF)
option(LLVP_ENABLE_OPENCV "Build OpenCV capture and tests" OFF)

if(LLVP_ENABLE_ASAN_UBSAN AND LLVP_ENABLE_TSAN)
    message(FATAL_ERROR "ASan/UBSan and TSan must use separate builds")
endif()

add_library(llvp_spsc INTERFACE)
target_compile_features(llvp_spsc INTERFACE cxx_std_20)
target_include_directories(llvp_spsc INTERFACE
    ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(llvp_spsc INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(LLVP_ENABLE_ASAN_UBSAN)
    target_compile_options(llvp_spsc INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(llvp_spsc INTERFACE -fsanitize=address,undefined)
elseif(LLVP_ENABLE_TSAN)
    target_compile_options(llvp_spsc INTERFACE
        -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(llvp_spsc INTERFACE -fsanitize=thread)
endif()

if(LLVP_BUILD_TESTS)
    include(CTest)
    add_executable(config_test tests/config_test.cpp src/app/config.cpp)
    target_link_libraries(config_test PRIVATE llvp_spsc)
    add_test(NAME config_test COMMAND config_test)
    add_executable(ring_buffer_test tests/ring_buffer_test.cpp)
    target_link_libraries(ring_buffer_test PRIVATE llvp_spsc)
    add_test(NAME ring_buffer_test COMMAND ring_buffer_test)
    add_executable(frame_selector_test tests/frame_selector_test.cpp)
    target_link_libraries(frame_selector_test PRIVATE llvp_spsc)
    add_test(NAME frame_selector_test COMMAND frame_selector_test)
endif()

add_executable(synthetic_pipeline src/synthetic_pipeline.cpp)
target_link_libraries(synthetic_pipeline PRIVATE llvp_spsc)

if(LLVP_BUILD_BENCHMARKS)
    add_executable(ring_benchmark benchmarks/ring_benchmark.cpp)
    target_link_libraries(ring_benchmark PRIVATE llvp_spsc)
endif()

if(LLVP_BUILD_TORCH_PROBE)
    find_package(Torch REQUIRED)
    add_executable(torch_model_probe tools/torch_model_probe.cpp)
    target_compile_features(torch_model_probe PRIVATE cxx_std_20)
    target_link_libraries(torch_model_probe PRIVATE "${TORCH_LIBRARIES}")
endif()

if(LLVP_ENABLE_OPENCV)
    include(FetchContent)
    set(BUILD_LIST core,imgproc,imgcodecs,videoio,highgui CACHE STRING "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
    set(BUILD_JAVA OFF CACHE BOOL "" FORCE)
    set(BUILD_opencv_python3 OFF CACHE BOOL "" FORCE)
    set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(opencv
        GIT_REPOSITORY https://github.com/opencv/opencv.git
        GIT_TAG 4.14.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(opencv)

    add_library(llvp_opencv_capture src/capture/opencv_source.cpp)
    target_link_libraries(llvp_opencv_capture PUBLIC llvp_spsc opencv_videoio)
    target_include_directories(llvp_opencv_capture PUBLIC ${PROJECT_SOURCE_DIR}/include)
    target_include_directories(llvp_opencv_capture SYSTEM PUBLIC
        ${opencv_SOURCE_DIR}/modules/core/include
        ${opencv_SOURCE_DIR}/modules/videoio/include
        ${opencv_SOURCE_DIR}/modules/imgcodecs/include
        ${opencv_SOURCE_DIR}/modules/imgproc/include
        ${opencv_SOURCE_DIR}/modules/highgui/include
        ${PROJECT_BINARY_DIR})

    add_executable(create_fixture_video tools/create_fixture_video.cpp)
    target_link_libraries(create_fixture_video PRIVATE llvp_opencv_capture
                                                       opencv_imgcodecs)

    if(LLVP_BUILD_TESTS)
        add_executable(opencv_source_test tests/opencv_source_test.cpp)
        target_link_libraries(opencv_source_test PRIVATE llvp_opencv_capture
                                                         opencv_imgcodecs)
        add_test(NAME opencv_source_test COMMAND opencv_source_test)
    endif()

    if(LLVP_BUILD_TORCH_PROBE)
        add_library(llvp_torch_detector src/inference/torch_detector.cpp)
        target_include_directories(llvp_torch_detector PUBLIC
            ${PROJECT_SOURCE_DIR}/include)
        target_link_libraries(llvp_torch_detector PUBLIC llvp_opencv_capture
            opencv_imgproc "${TORCH_LIBRARIES}")

        add_executable(low_latency_detector
            app/main.cpp
            src/app/config.cpp
            src/pipeline/video_pipeline.cpp
            src/display/draw_detections.cpp)
        target_link_libraries(low_latency_detector PRIVATE llvp_torch_detector
                                                           opencv_highgui)

        if(LLVP_BUILD_TESTS)
            add_executable(torch_detector_test tests/torch_detector_test.cpp)
            target_link_libraries(torch_detector_test PRIVATE llvp_torch_detector
                                                              opencv_imgcodecs)
            target_compile_definitions(torch_detector_test PRIVATE
                LLVP_MODEL_PATH="${PROJECT_SOURCE_DIR}/models/ssdlite320.pt"
                LLVP_TORCHVISION_OPS_PATH="${PROJECT_SOURCE_DIR}/.venv/lib/python3.14/site-packages/torchvision/_C_stable.so"
                LLVP_FIXTURE_IMAGE="${PROJECT_SOURCE_DIR}/models/fixtures/dog.jpg")
            add_test(NAME torch_detector_test COMMAND torch_detector_test)
        endif()
    endif()
endif()

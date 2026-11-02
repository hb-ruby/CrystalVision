#pragma once

#include "detect.hpp"
#include "frame.hpp"

#include <onnxruntime_cxx_api.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace crystal {

// Runs the NudeNet v3 detector on whole images with ONNX Runtime (CPU).
class Detector {
  public:
    struct Options {
        float minScore = 0.25f;     // NudeNet's NMS score threshold
        float iouThreshold = 0.45f; // NudeNet's NMS overlap threshold
        int threads = 0;            // intra-op threads; 0 = ONNX Runtime default (one per core)
    };

    struct Timing {
        double preMs = 0;   // letterbox + tensor fill
        double inferMs = 0; // session.Run
        double postMs = 0;  // decode + NMS
    };

    // Loads the model and checks its input/output shapes. Throws Ort::Exception or
    // std::runtime_error.
    Detector(const std::filesystem::path& model, Options opts);
    explicit Detector(const std::filesystem::path& model) : Detector{model, Options{}} {}

    // Detections in `image` pixels, highest score first.
    std::vector<Detection> detect(const Frame& image, Timing* timing = nullptr);

    int inputSize() const { return inputSize_; }
    std::size_t anchors() const { return anchors_; }

  private:
    Options opts_;
    Ort::Env env_;
    Ort::Session session_{nullptr};
    std::string inputName_;
    std::string outputName_;
    int inputSize_ = 640;
    std::size_t anchors_ = 0;
    std::vector<float> input_; // reused between calls
};

} // namespace crystal

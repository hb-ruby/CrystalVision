#include "detector.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace crystal {

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

std::string shapeString(const std::vector<std::int64_t>& shape) {
    std::string s = "[";
    for (std::size_t i = 0; i < shape.size(); ++i) s += (i ? ", " : "") + std::to_string(shape[i]);
    return s + "]";
}

} // namespace

Detector::Detector(const std::filesystem::path& model, Options opts)
    : opts_{opts}, env_{ORT_LOGGING_LEVEL_WARNING, "crystalvision"} {
    Ort::SessionOptions so;
    so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    if (opts_.threads > 0) so.SetIntraOpNumThreads(opts_.threads);
    session_ = Ort::Session{env_, model.c_str(), so};

    if (session_.GetInputCount() != 1 || session_.GetOutputCount() != 1)
        throw std::runtime_error{"unexpected model: want exactly one input and one output"};

    Ort::AllocatorWithDefaultOptions alloc;
    inputName_ = session_.GetInputNameAllocated(0, alloc).get();
    outputName_ = session_.GetOutputNameAllocated(0, alloc).get();

    // Input: [1, 3, S, S]. A dynamic size (-1) falls back to 640, NudeNet's default.
    const auto in = session_.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (in.size() != 4 || (in[1] != 3 && in[1] != -1))
        throw std::runtime_error{"unexpected model input shape " + shapeString(in)};
    if (in[2] > 0) inputSize_ = static_cast<int>(in[2]);

    // Output: [1, 4 + classes, anchors].
    const auto out = session_.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (out.size() != 3 || (out[1] > 0 && out[1] != static_cast<std::int64_t>(4 + kLabels.size())))
        throw std::runtime_error{"unexpected model output shape " + shapeString(out) +
                                 " (expected [1, " + std::to_string(4 + kLabels.size()) + ", N])"};
}

std::vector<Detection> Detector::detect(const Frame& image, Timing* timing) {
    auto t = Clock::now();
    const Letterbox lb = letterbox(image, inputSize_, input_);
    if (timing) timing->preMs = msSince(t);

    t = Clock::now();
    const std::array<std::int64_t, 4> inShape = {1, 3, inputSize_, inputSize_};
    const Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inTensor = Ort::Value::CreateTensor<float>(mem, input_.data(), input_.size(),
                                                          inShape.data(), inShape.size());
    const char* inNames[] = {inputName_.c_str()};
    const char* outNames[] = {outputName_.c_str()};
    std::vector<Ort::Value> outputs =
        session_.Run(Ort::RunOptions{nullptr}, inNames, &inTensor, 1, outNames, 1);
    if (timing) timing->inferMs = msSince(t);

    t = Clock::now();
    const auto info = outputs[0].GetTensorTypeAndShapeInfo();
    const auto shape = info.GetShape();
    if (shape.size() != 3 || shape[0] != 1 ||
        shape[1] != static_cast<std::int64_t>(4 + kLabels.size()) || shape[2] <= 0)
        throw std::runtime_error{"unexpected model output shape " + shapeString(shape)};
    anchors_ = static_cast<std::size_t>(shape[2]);

    const std::span<const float> data{outputs[0].GetTensorData<float>(), info.GetElementCount()};
    std::vector<Detection> dets =
        nms(decode(data, kLabels.size(), anchors_, lb, opts_.minScore), opts_.iouThreshold);
    if (timing) timing->postMs = msSince(t);
    return dets;
}

} // namespace crystal

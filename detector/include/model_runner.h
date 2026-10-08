#pragma once

#include <string>
#include <vector>
#include <memory>

namespace badbt {

class ModelRunner {
public:
    ModelRunner();
    explicit ModelRunner(const std::string& model_path);
    ~ModelRunner();

    ModelRunner(ModelRunner&&) noexcept;
    ModelRunner& operator=(ModelRunner&&) noexcept;
    ModelRunner(const ModelRunner&) = delete;
    ModelRunner& operator=(const ModelRunner&) = delete;

    // Load ONNX model from file. Returns true on success.
    bool load(const std::string& model_path);

    // Check if model is currently loaded and ready for inference
    bool is_loaded() const;

    // Run inference on canonical 12-element feature vector.
    // Returns P(injected) in [0.0, 1.0], or -1.0f on error.
    float predict_proba_injected(const std::vector<float>& features);

    // Path of the loaded model
    const std::string& model_path() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace badbt

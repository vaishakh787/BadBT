#include "model_runner.h"
#include "path_utils.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Workaround for MinGW-w64 GCC where _stdcall is defined as empty on x64
#if defined(__GNUC__) && !defined(_stdcall)
#define _stdcall
#endif

#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <array>
#include <vector>

namespace badbt {

static std::wstring to_wstring(const std::string& str) {
    if (str.empty()) return L"";
#if defined(_WIN32)
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &result[0], size);
    return result;
#else
    return std::wstring(str.begin(), str.end());
#endif
}

struct ModelRunner::Impl {
    std::string model_path_;
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::SessionOptions> session_options_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    bool is_loaded_ = false;

    Impl() {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "BadBTDetector");
        session_options_ = std::make_unique<Ort::SessionOptions>();
        session_options_->SetIntraOpNumThreads(1);
        session_options_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    }

    bool load(const std::string& path) {
        try {
            std::string resolved_path = find_model_path(path);
            model_path_ = resolved_path;
            std::wstring wpath = to_wstring(resolved_path);
            session_ = std::make_unique<Ort::Session>(*env_, wpath.c_str(), *session_options_);
            is_loaded_ = true;
            return true;
        } catch (const Ort::Exception& e) {
            std::cerr << "[ModelRunner] ONNX Runtime error loading " << model_path_ << ": " << e.what() << "\n";
            is_loaded_ = false;
            session_.reset();
            return false;
        } catch (const std::exception& e) {
            std::cerr << "[ModelRunner] Standard error loading " << path << ": " << e.what() << "\n";
            is_loaded_ = false;
            session_.reset();
            return false;
        }
    }

    float predict(const std::vector<float>& features) {
        if (!is_loaded_ || !session_) {
            std::cerr << "[ModelRunner] Error: Model not loaded.\n";
            return -1.0f;
        }
        if (features.size() != 12) {
            std::cerr << "[ModelRunner] Error: Expected 12 features, got " << features.size() << "\n";
            return -1.0f;
        }

        try {
            std::array<int64_t, 2> input_shape = {1, 12};
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                memory_info_,
                const_cast<float*>(features.data()),
                features.size(),
                input_shape.data(),
                input_shape.size()
            );

            const char* input_names[] = {"float_input"};
            const char* output_names[] = {"probabilities"};

            auto output_tensors = session_->Run(
                Ort::RunOptions{nullptr},
                input_names,
                &input_tensor,
                1,
                output_names,
                1
            );

            if (output_tensors.empty()) {
                std::cerr << "[ModelRunner] Error: No output tensors returned.\n";
                return -1.0f;
            }

            const float* prob_data = output_tensors[0].GetTensorMutableData<float>();
            // output is shape [1, 2] -> prob_data[0] = P(human), prob_data[1] = P(injected)
            return prob_data[1];

        } catch (const Ort::Exception& e) {
            std::cerr << "[ModelRunner] ONNX inference error: " << e.what() << "\n";
            return -1.0f;
        } catch (const std::exception& e) {
            std::cerr << "[ModelRunner] Inference exception: " << e.what() << "\n";
            return -1.0f;
        }
    }
};

ModelRunner::ModelRunner() : impl_(std::make_unique<Impl>()) {}

ModelRunner::ModelRunner(const std::string& model_path) : impl_(std::make_unique<Impl>()) {
    load(model_path);
}

ModelRunner::~ModelRunner() = default;

ModelRunner::ModelRunner(ModelRunner&&) noexcept = default;
ModelRunner& ModelRunner::operator=(ModelRunner&&) noexcept = default;

bool ModelRunner::load(const std::string& model_path) {
    return impl_->load(model_path);
}

bool ModelRunner::is_loaded() const {
    return impl_->is_loaded_;
}

float ModelRunner::predict_proba_injected(const std::vector<float>& features) {
    return impl_->predict(features);
}

const std::string& ModelRunner::model_path() const {
    return impl_->model_path_;
}

} // namespace badbt

#pragma once

#include "feature_extractor.h"
#include "model_runner.h"
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

namespace badbt {

enum class DetectorState {
    Monitoring,
    Cooldown
};

struct DetectorConfig {
    size_t      window_size = 25;
    size_t      window_step = 1;
    float       threshold = 0.95f;
    double      cooldown_seconds = 30.0;
    std::string model_path = "model/rf_model.onnx";
};

struct DetectionResult {
    int64_t             timestamp_ns = 0;
    size_t              window_seq = 0;
    float               proba_injected = 0.0f;
    bool                is_injected = false;
    bool                alert_triggered = false;  // True if is_injected and state was Monitoring
    std::vector<float>  features;
    std::string         primary_device_path;
};

using DetectionCallback = std::function<void(const DetectionResult&)>;

class LiveDetector {
public:
    explicit LiveDetector(const DetectorConfig& config = DetectorConfig());

    // Initialize/load model
    bool initialize();

    // Set callback invoked whenever a window is evaluated
    void set_detection_callback(DetectionCallback cb);

    // Process a raw keyboard event. Returns true if an evaluation was triggered.
    bool process_event(const InputEvent& event);

    // Process a pre-paired keystroke. Returns true if an evaluation was triggered.
    bool process_paired(const PairedKeystroke& paired);

    // Replay a raw JSONL file. Returns count of evaluated windows.
    size_t replay_jsonl(const std::string& file_path);

    // Full reset (clears buffers, counters, and returns to Monitoring)
    void reset();

    // Reset detection state only (clears feature extractor buffer & returns state to Monitoring)
    void reset_detection_state();

    // Check if cooldown has expired at given timestamp (or current time if 0).
    // If expired, transitions state to Monitoring and resets detection state.
    // Returns true if state transitioned from Cooldown to Monitoring.
    bool check_cooldown_expiry(int64_t current_ts_ns = 0);

    // Stats & Accessors
    const DetectorConfig& config() const { return config_; }
    DetectorState state() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return state_;
    }
    size_t total_events() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return total_events_;
    }
    size_t total_paired() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return feature_extractor_.paired_count();
    }
    size_t total_windows() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return total_windows_;
    }
    size_t total_injected_windows() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return total_injected_windows_;
    }
    size_t total_alerts() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return total_alerts_;
    }
    float max_proba_injected() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return max_proba_;
    }
    int64_t last_alert_timestamp_ns() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return last_alert_ts_;
    }

private:
    void evaluate_current_window(int64_t event_ts);
    void reset_detection_state_locked();

    DetectorConfig config_;
    FeatureExtractor feature_extractor_;
    ModelRunner model_runner_;
    DetectionCallback callback_;

    mutable std::recursive_mutex mutex_;
    DetectorState state_ = DetectorState::Monitoring;

    size_t total_events_ = 0;
    size_t total_windows_ = 0;
    size_t total_injected_windows_ = 0;
    size_t total_alerts_ = 0;
    float max_proba_ = 0.0f;
    int64_t last_alert_ts_ = -1;
};

} // namespace badbt

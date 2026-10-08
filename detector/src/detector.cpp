#include "detector.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace badbt {

LiveDetector::LiveDetector(const DetectorConfig& config)
    : config_(config),
      feature_extractor_(config.window_size, config.window_step) {
    if (!config_.model_path.empty()) {
        initialize();
    }
}

bool LiveDetector::initialize() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (config_.model_path.empty()) {
        std::cerr << "[LiveDetector] Error: Empty model path.\n";
        return false;
    }
    return model_runner_.load(config_.model_path);
}

void LiveDetector::set_detection_callback(DetectionCallback cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    callback_ = std::move(cb);
}

void LiveDetector::reset_detection_state_locked() {
    feature_extractor_.reset();
    state_ = DetectorState::Monitoring;
}

void LiveDetector::reset_detection_state() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reset_detection_state_locked();
}

bool LiveDetector::check_cooldown_expiry(int64_t current_ts_ns) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (state_ != DetectorState::Cooldown) {
        return false;
    }

    if (current_ts_ns <= 0) {
        auto now = std::chrono::system_clock::now();
        current_ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();
    }

    double cooldown_ns = config_.cooldown_seconds * 1e9;
    if (last_alert_ts_ >= 0 && static_cast<double>(current_ts_ns - last_alert_ts_) >= cooldown_ns) {
        reset_detection_state_locked();
        return true;
    }
    return false;
}

void LiveDetector::reset() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reset_detection_state_locked();
    total_events_ = 0;
    total_windows_ = 0;
    total_injected_windows_ = 0;
    total_alerts_ = 0;
    max_proba_ = 0.0f;
    last_alert_ts_ = -1;
}

bool LiveDetector::process_event(const InputEvent& event) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    total_events_++;

    // Check if cooldown has elapsed before pushing new event into buffer
    if (event.timestamp_ns > 0) {
        check_cooldown_expiry(event.timestamp_ns);
    }

    bool ready = feature_extractor_.push_event(event);
    if (ready) {
        evaluate_current_window(event.timestamp_ns);
    }
    return ready;
}

bool LiveDetector::process_paired(const PairedKeystroke& paired) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Check if cooldown has elapsed before pushing pre-paired keystroke
    if (paired.keydown_ts > 0) {
        check_cooldown_expiry(paired.keydown_ts);
    }

    bool ready = feature_extractor_.push_paired(paired);
    if (ready) {
        evaluate_current_window(paired.keydown_ts);
    }
    return ready;
}

void LiveDetector::evaluate_current_window(int64_t event_ts) {
    total_windows_++;
    auto feats = feature_extractor_.extract_window_features();
    float proba = model_runner_.predict_proba_injected(feats);

    if (proba > max_proba_) {
        max_proba_ = proba;
    }

    bool is_injected = (proba >= config_.threshold);
    bool alert = false;

    if (is_injected) {
        total_injected_windows_++;
        if (state_ == DetectorState::Monitoring) {
            alert = true;
            state_ = DetectorState::Cooldown;
            last_alert_ts_ = event_ts;
            total_alerts_++;
        }
    }

    DetectionResult res;
    res.timestamp_ns = event_ts;
    res.window_seq = total_windows_;
    res.proba_injected = proba;
    res.is_injected = is_injected;
    res.alert_triggered = alert;
    res.features = std::move(feats);

    if (!feature_extractor_.current_window().empty()) {
        res.primary_device_path = feature_extractor_.current_window().back().device_path;
    }

    if (callback_) {
        callback_(res);
    }
}

size_t LiveDetector::replay_jsonl(const std::string& file_path) {
    std::ifstream infile(file_path);
    if (!infile.is_open()) {
        std::cerr << "[LiveDetector] Error: Cannot open " << file_path << "\n";
        return 0;
    }

    std::string line;
    while (std::getline(infile, line)) {
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            InputEvent ev;
            ev.timestamp_ns = j.value("timestamp_ns", 0LL);
            ev.vkey = static_cast<uint16_t>(j.value("vkey", 0));
            ev.scan_code = static_cast<uint16_t>(j.value("scan_code", 0));
            ev.flags = static_cast<uint16_t>(j.value("flags", 0));
            ev.is_autorepeat = j.value("is_autorepeat", false);
            ev.device_path = j.value("device_path", "");

            process_event(ev);
        } catch (const std::exception& e) {
            std::cerr << "[LiveDetector] Warning: JSON parse error on line: " << e.what() << "\n";
        }
    }

    return total_windows_;
}

} // namespace badbt

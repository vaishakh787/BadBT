#pragma once

#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <cstdint>
#include <cmath>

namespace badbt {

// Matches RawKeyEvent from capture/src/raw_input.h
struct InputEvent {
    int64_t     timestamp_ns = 0;
    uint16_t    vkey = 0;
    uint16_t    scan_code = 0;
    uint16_t    flags = 0;         // RI_KEY_BREAK bit (0x01) marks keyup; RI_KEY_E0 (0x02) marks extended key
    bool        is_autorepeat = false;
    std::string device_path;
};

// Represents a completed keydown/keyup pair
struct PairedKeystroke {
    uint64_t    seq = 0;
    int64_t     keydown_ts = 0;
    uint16_t    vkey = 0;
    uint16_t    scan_code = 0;
    double      dwell_ms = 0.0;
    double      flight_ms = 0.0;   // NaN if no preceding keyup
    bool        is_autorepeat = false;
    std::string device_path;
};

class FeatureExtractor {
public:
    static constexpr size_t CANONICAL_FEATURE_COUNT = 12;
    static constexpr float SENTINEL_VALUE = -999.0f;
    static constexpr double DWELL_CAP_MS = 2000.0;
    static constexpr double PAUSE_THRESHOLD_MS = 2000.0;

    explicit FeatureExtractor(size_t window_size = 25, size_t step = 1);

    // Push a raw key event (keydown or keyup).
    // Returns true when a new evaluation window is ready (buffer has >= window_size and step condition is met).
    bool push_event(const InputEvent& event);

    // Push a pre-paired keystroke directly (useful for tests and offline validation).
    // Returns true when an evaluation window is ready.
    bool push_paired(const PairedKeystroke& paired);

    // Compute the canonical 12-feature vector for the current window.
    // Throws or returns sentinels if window has fewer than window_size keystrokes.
    std::vector<float> extract_window_features() const;

    // Reset all internal pairing and rolling window state.
    void reset();

    // Accessors
    size_t window_size() const { return window_size_; }
    size_t step() const { return step_; }
    size_t paired_count() const { return total_paired_count_; }
    size_t evaluated_window_count() const { return evaluated_window_count_; }
    const std::deque<PairedKeystroke>& current_window() const { return window_buffer_; }

private:
    size_t window_size_;
    size_t step_;

    // Keydown state for pairing: key_id -> open key info
    // key_id = (scan_code << 1) | (is_extended ? 1 : 0)
    struct OpenKey {
        int64_t     keydown_ts = 0;
        uint16_t    vkey = 0;
        uint16_t    scan_code = 0;
        double      flight_ms = 0.0;
        bool        is_autorepeat = false;
        std::string device_path;
        uint64_t    seq = 0;
    };

    std::unordered_map<uint32_t, OpenKey> open_keys_;
    int64_t last_keyup_ts_ = -1;
    uint64_t seq_counter_ = 0;

    // Deduplication tracking (matches Python clean_session dedup)
    InputEvent last_event_{};
    bool has_last_event_ = false;

    // Rolling window buffer
    std::deque<PairedKeystroke> window_buffer_;
    size_t total_paired_count_ = 0;
    size_t keystrokes_since_last_eval_ = 0;
    size_t evaluated_window_count_ = 0;
};

} // namespace badbt

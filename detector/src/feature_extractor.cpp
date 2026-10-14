#include "feature_extractor.h"
#include <numeric>
#include <unordered_set>
#include <algorithm>

namespace badbt {

FeatureExtractor::FeatureExtractor(size_t window_size, size_t step)
    : window_size_(window_size), step_(step) {
    // Dedup horizon = 4x the window, in raw events. A window of window_size_ paired
    // keystrokes typically spans ~2x window_size_ raw events, so 4x leaves headroom.
    // This is a bounded-memory heuristic, NOT a guarantee: held keys, autorepeat,
    // unmatched keyups and other unpaired events can evict a still-relevant event,
    // and a duplicate arriving after eviction can alter pairing, dwell, flight or
    // pause features. Exact unbounded per-session dedup is traded for bounded memory.
    // window_size_ == 0 falls back to a horizon of 8.
    dedup_horizon_ = (window_size_ > 0) ? window_size_ * 4 : 8;
}

void FeatureExtractor::reset() {
    open_keys_.clear();
    last_keyup_ts_ = -1;
    seq_counter_ = 0;
    seen_events_.clear();
    dedup_order_.clear();
    window_buffer_.clear();
    total_paired_count_ = 0;
    keystrokes_since_last_eval_ = 0;
    evaluated_window_count_ = 0;
}

bool FeatureExtractor::push_event(const InputEvent& event) {
    // ── Bounded exact-duplicate removal (runs before any pairing state update) ──
    // Drop exact-duplicate raw events (timestamp_ns, vkey, scan_code, flags),
    // keep-first, over the last dedup_horizon_ arrived events. This mirrors Python
    // drop_duplicates(subset=[...]) within the realistic (adjacent) horizon and
    // discards the event before it can perturb dwell/flight math -- exactly as Python
    // removes duplicate rows before pair_dwell_flight(). A duplicate older than the
    // horizon is intentionally NOT removed (documented divergence; see header).
    //
    // NOTE on device paths: unlike Python's clean_session(), the live path does NOT
    // drop "minority device" events. That filter required a whole-session two-pass
    // count; doing it incrementally is order-dependent and could discard events from
    // the real keyboard, blinding detection. device_path still flows to features, so
    // a genuine multi-interface window reports n_devices_in_window >= 2 (a residual
    // live/training distribution difference -- the training set has no such session).
    const DedupKey dedup_key{event.timestamp_ns, event.vkey,
                             event.scan_code, event.flags};
    if (!seen_events_.insert(dedup_key).second) {
        return false; // seen within the horizon -> duplicate, drop
    }
    dedup_order_.push_back(dedup_key);
    if (dedup_order_.size() > dedup_horizon_) {
        seen_events_.erase(dedup_order_.front()); // prune oldest-by-arrival
        dedup_order_.pop_front();
    }

    // 2. Parse flags
    bool is_keyup = (event.flags & 0x01) != 0;      // RI_KEY_BREAK (0x01)
    bool is_extended = (event.flags & 0x02) != 0;   // RI_KEY_E0 (0x02)
    uint32_t key_id = (static_cast<uint32_t>(event.scan_code) << 1) | (is_extended ? 1u : 0u);

    if (!is_keyup) {
        // Keydown: record flight time from preceding keyup
        double flight_ms = std::numeric_limits<double>::quiet_NaN();
        if (last_keyup_ts_ >= 0) {
            flight_ms = static_cast<double>(event.timestamp_ns - last_keyup_ts_) / 1e6;
        }

        OpenKey ok;
        ok.keydown_ts = event.timestamp_ns;
        ok.vkey = event.vkey;
        ok.scan_code = event.scan_code;
        ok.flight_ms = flight_ms;
        ok.is_autorepeat = event.is_autorepeat;
        ok.device_path = event.device_path;
        ok.seq = ++seq_counter_;

        open_keys_[key_id] = ok;
        return false;
    } else {
        // Keyup: update last_keyup_ts_ regardless of whether an open key was found
        last_keyup_ts_ = event.timestamp_ns;

        auto it = open_keys_.find(key_id);
        if (it != open_keys_.end()) {
            OpenKey ok = it->second;
            open_keys_.erase(it);

            double dwell_ms = static_cast<double>(event.timestamp_ns - ok.keydown_ts) / 1e6;

            PairedKeystroke paired;
            paired.seq = ok.seq;
            paired.keydown_ts = ok.keydown_ts;
            paired.vkey = ok.vkey;
            paired.scan_code = ok.scan_code;
            paired.dwell_ms = dwell_ms;
            paired.flight_ms = ok.flight_ms;
            paired.is_autorepeat = ok.is_autorepeat;
            paired.device_path = ok.device_path;

            return push_paired(paired);
        }
        return false;
    }
}

bool FeatureExtractor::push_paired(const PairedKeystroke& paired) {
    window_buffer_.push_back(paired);
    total_paired_count_++;
    keystrokes_since_last_eval_++;

    if (window_buffer_.size() > window_size_) {
        window_buffer_.pop_front();
    }

    if (window_buffer_.size() >= window_size_ && keystrokes_since_last_eval_ >= step_) {
        keystrokes_since_last_eval_ = 0;
        evaluated_window_count_++;
        return true;
    }

    return false;
}

std::vector<float> FeatureExtractor::extract_window_features() const {
    std::vector<float> feats(CANONICAL_FEATURE_COUNT, SENTINEL_VALUE);
    if (window_buffer_.size() < window_size_) {
        return feats;
    }

    // ── 1. Dwell features ──
    std::vector<double> valid_dwells;
    valid_dwells.reserve(window_size_);
    int n_dwell_outliers = 0;

    for (const auto& k : window_buffer_) {
        if (std::isnan(k.dwell_ms)) {
            continue;
        }
        if (k.dwell_ms > DWELL_CAP_MS) {
            n_dwell_outliers++;
        } else {
            valid_dwells.push_back(k.dwell_ms);
        }
    }

    float mean_dwell = SENTINEL_VALUE;
    float std_dwell = SENTINEL_VALUE;
    float cv_dwell_val = SENTINEL_VALUE;

    if (!valid_dwells.empty()) {
        double sum = 0.0;
        for (double d : valid_dwells) sum += d;
        double m = sum / valid_dwells.size();
        mean_dwell = static_cast<float>(m);

        if (valid_dwells.size() >= 2) {
            double sq_sum = 0.0;
            for (double d : valid_dwells) {
                double diff = d - m;
                sq_sum += diff * diff;
            }
            double s = std::sqrt(sq_sum / (valid_dwells.size() - 1)); // ddof=1
            std_dwell = static_cast<float>(s);

            if (std::isfinite(m) && m != 0.0 && std::isfinite(s)) {
                cv_dwell_val = static_cast<float>(s / m);
            }
        }
    }

    // ── 2. Flight features ──
    std::vector<double> valid_flights;
    valid_flights.reserve(window_size_);
    int n_pauses = 0;

    for (const auto& k : window_buffer_) {
        if (std::isnan(k.flight_ms)) {
            continue;
        }
        if (k.flight_ms > PAUSE_THRESHOLD_MS) {
            n_pauses++;
        } else {
            valid_flights.push_back(k.flight_ms);
        }
    }

    float mean_flight = SENTINEL_VALUE;
    float std_flight = SENTINEL_VALUE;
    float cv_flight_val = SENTINEL_VALUE;

    if (!valid_flights.empty()) {
        double sum = 0.0;
        for (double f : valid_flights) sum += f;
        double m = sum / valid_flights.size();
        mean_flight = static_cast<float>(m);

        if (valid_flights.size() >= 2) {
            double sq_sum = 0.0;
            for (double f : valid_flights) {
                double diff = f - m;
                sq_sum += diff * diff;
            }
            double s = std::sqrt(sq_sum / (valid_flights.size() - 1)); // ddof=1
            std_flight = static_cast<float>(s);

            if (std::isfinite(m) && m != 0.0 && std::isfinite(s)) {
                cv_flight_val = static_cast<float>(s / m);
            }
        }
    }

    // ── 3. Ratios and counts ──
    int autorepeat_count = 0;
    int backspace_count = 0;
    std::unordered_set<std::string> devices;

    for (const auto& k : window_buffer_) {
        if (k.is_autorepeat) {
            autorepeat_count++;
        }
        // VK_BACK = 0x08, VK_DELETE = 0x2E
        if (k.vkey == 0x08 || k.vkey == 0x2E) {
            backspace_count++;
        }
        if (!k.device_path.empty()) {
            devices.insert(k.device_path);
        }
    }

    float autorepeat_ratio = static_cast<float>(autorepeat_count) / static_cast<float>(window_size_);
    float backspace_ratio = static_cast<float>(backspace_count) / static_cast<float>(window_size_);
    float n_devices_in_window = static_cast<float>(devices.empty() ? 1 : devices.size());

    // ── 4. Events per second ──
    int64_t span_ns = window_buffer_.back().keydown_ts - window_buffer_.front().keydown_ts;
    float events_per_sec = SENTINEL_VALUE;
    if (span_ns > 0) {
        double span_s = static_cast<double>(span_ns) / 1e9;
        events_per_sec = static_cast<float>(static_cast<double>(window_size_) / span_s);
    }

    // ── 5. Pack in exact canonical 12-feature order ──
    feats[0]  = mean_dwell;
    feats[1]  = std_dwell;
    feats[2]  = cv_dwell_val;
    feats[3]  = static_cast<float>(n_dwell_outliers);
    feats[4]  = mean_flight;
    feats[5]  = std_flight;
    feats[6]  = cv_flight_val;
    feats[7]  = static_cast<float>(n_pauses);
    feats[8]  = autorepeat_ratio;
    feats[9]  = backspace_ratio;
    feats[10] = events_per_sec;
    feats[11] = n_devices_in_window;

    return feats;
}

} // namespace badbt

#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>
#include <cassert>
#include <iomanip>
#include <nlohmann/json.hpp>

#include "../include/feature_extractor.h"

static bool approx_equal(float a, float b, float tol = 1e-4f) {
    if (std::isnan(a) && std::isnan(b)) return true;
    return std::fabs(a - b) <= tol;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "        TEST 1: C++ / Python Feature Parity Test        \n";
    std::cout << "========================================================\n";

    // ── Part A: Replay adhi_01.jsonl until 1st window is formed ──
    std::cout << "\n[Part A] Verifying Window 0 against features.csv Row 0...\n";

    const std::string raw_path = "data/raw/adhi_01.jsonl";
    std::ifstream infile(raw_path);
    if (!infile.is_open()) {
        std::cerr << "FAILED: Cannot open " << raw_path << "\n";
        return 1;
    }

    badbt::FeatureExtractor extractor(25, 1);
    std::string line;
    bool first_window_ready = false;

    while (std::getline(infile, line)) {
        if (line.empty()) continue;
        auto j = nlohmann::json::parse(line);
        badbt::InputEvent ev;
        ev.timestamp_ns = j.value("timestamp_ns", 0LL);
        ev.vkey = static_cast<uint16_t>(j.value("vkey", 0));
        ev.scan_code = static_cast<uint16_t>(j.value("scan_code", 0));
        ev.flags = static_cast<uint16_t>(j.value("flags", 0));
        ev.is_autorepeat = j.value("is_autorepeat", false);
        ev.device_path = j.value("device_path", "");

        if (extractor.push_event(ev)) {
            first_window_ready = true;
            break;
        }
    }

    if (!first_window_ready) {
        std::cerr << "FAILED: Did not obtain first window from " << raw_path << "\n";
        return 1;
    }

    auto feats = extractor.extract_window_features();
    assert(feats.size() == 12);

    // Ground truth from data/processed/features.csv Row 0:
    // mean_dwell_ms: 110.598284
    // std_dwell_ms: 22.893003596944926
    // cv_dwell: 0.20699239417625073
    // n_dwell_outliers_excluded: 0
    // mean_flight_ms: 207.45681249999998
    // std_flight_ms: 217.06385584065652
    // cv_flight: 1.046308642386263
    // n_pauses_excluded: 0
    // autorepeat_ratio: 0.0
    // backspace_ratio: 0.0
    // events_per_sec: 3.286752426188612
    // n_devices_in_window: 1
    const std::vector<float> expected_row0 = {
        110.598284f,
        22.893004f,
        0.2069924f,
        0.0f,
        207.456812f,
        217.063856f,
        1.046309f,
        0.0f,
        0.0f,
        0.0f,
        3.286752f,
        1.0f
    };

    const char* feature_names[] = {
        "mean_dwell_ms",
        "std_dwell_ms",
        "cv_dwell",
        "n_dwell_outliers_excluded",
        "mean_flight_ms",
        "std_flight_ms",
        "cv_flight",
        "n_pauses_excluded",
        "autorepeat_ratio",
        "backspace_ratio",
        "events_per_sec",
        "n_devices_in_window"
    };

    bool all_match = true;
    for (size_t i = 0; i < 12; ++i) {
        float actual = feats[i];
        float expected = expected_row0[i];
        float diff = std::fabs(actual - expected);
        std::cout << "  [" << std::setw(2) << i << "] "
                  << std::left << std::setw(27) << feature_names[i]
                  << " Expected: " << std::fixed << std::setprecision(6) << std::setw(12) << expected
                  << " Actual: " << std::setw(12) << actual
                  << " Diff: " << std::scientific << std::setprecision(2) << diff;

        if (diff < 1e-3f) {
            std::cout << "  [PASS]\n";
        } else {
            std::cout << "  [FAIL]\n";
            all_match = false;
        }
    }

    if (!all_match) {
        std::cerr << "FAIL: Feature mismatch on Row 0!\n";
        return 1;
    }
    std::cout << ">>> Part A PASSED: C++ extractor exactly matches Python features.csv Row 0!\n\n";

    // ── Part B: Edge Cases & Sentinel Behavior ──
    std::cout << "[Part B] Testing Edge Cases & Sentinels...\n";

    badbt::FeatureExtractor edge_extractor(25, 1);
    // 1. Before 25 keystrokes -> all sentinels (-999.0f)
    auto empty_feats = edge_extractor.extract_window_features();
    for (float f : empty_feats) {
        assert(f == badbt::FeatureExtractor::SENTINEL_VALUE);
    }
    std::cout << "  - Empty buffer sentinels verified (-999.0f)\n";

    // 2. Synthetic window with dwell outlier (>2000ms), flight pause (>2000ms), backspaces
    edge_extractor.reset();
    for (size_t i = 0; i < 25; ++i) {
        badbt::PairedKeystroke pk;
        pk.seq = i + 1;
        pk.keydown_ts = 1'000'000'000LL + static_cast<int64_t>(i) * 100'000'000LL; // 100ms intervals
        pk.dwell_ms = (i == 0) ? 2500.0 : 50.0; // i=0 is outlier > 2000ms
        pk.flight_ms = (i == 1) ? 3000.0 : 50.0; // i=1 is pause > 2000ms
        pk.vkey = (i < 5) ? 0x08 : 0x41;         // 5 backspaces -> ratio 5/25 = 0.20
        pk.is_autorepeat = (i < 2);              // 2 autorepeats -> ratio 2/25 = 0.08
        pk.device_path = "DEV1";
        edge_extractor.push_paired(pk);
    }

    auto edge_feats = edge_extractor.extract_window_features();
    assert(edge_feats[3] == 1.0f); // 1 dwell outlier excluded
    assert(edge_feats[7] == 1.0f); // 1 flight pause excluded
    assert(approx_equal(edge_feats[0], 50.0f)); // mean dwell of remaining is 50.0
    assert(approx_equal(edge_feats[1], 0.0f));  // std dwell of identical remaining is 0.0
    assert(approx_equal(edge_feats[8], 2.0f / 25.0f)); // autorepeat ratio
    assert(approx_equal(edge_feats[9], 5.0f / 25.0f)); // backspace ratio
    assert(edge_feats[11] == 1.0f); // 1 device

    std::cout << "  - Outlier filtering & ratio edge cases verified\n";
    std::cout << ">>> Part B PASSED!\n\n";

    std::cout << "========================================================\n";
    std::cout << "         ALL FEATURE PARITY TESTS PASSED!               \n";
    std::cout << "========================================================\n";
    return 0;
}

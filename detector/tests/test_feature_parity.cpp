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

// Parity tolerance for all data-driven (Python-vs-C++) comparisons below.
// Rationale: features.csv is written from float64 pandas math; the C++ extractor
// accumulates in double but returns float32, so a few ULPs of drift is expected.
// 1e-3 is far tighter than any feature's between-class separation.
static constexpr float PARITY_TOL = 1e-3f;

static const char* kFeatureNames[12] = {
    "mean_dwell_ms", "std_dwell_ms", "cv_dwell", "n_dwell_outliers_excluded",
    "mean_flight_ms", "std_flight_ms", "cv_flight", "n_pauses_excluded",
    "autorepeat_ratio", "backspace_ratio", "events_per_sec", "n_devices_in_window"
};

// Replay one raw JSONL capture file through a fresh FeatureExtractor and return the
// features of the FIRST completed window. Mirrors the event parsing in Part A.
// Returns an empty vector if the file can't be opened or never fills a window.
static std::vector<float> first_window_of(const std::string& raw_path, size_t window, size_t step) {
    std::ifstream infile(raw_path);
    if (!infile.is_open()) {
        std::cerr << "FAILED: Cannot open " << raw_path << "\n";
        return {};
    }
    badbt::FeatureExtractor fx(window, step);
    std::string line;
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
        if (fx.push_event(ev)) {
            return fx.extract_window_features();
        }
    }
    return {};
}

// Compare a 12-feature vector against expected ground truth at PARITY_TOL, printing a
// per-feature table. Returns true only if every feature matches.
static bool compare_features(const std::string& label,
                             const std::vector<float>& actual,
                             const std::vector<float>& expected) {
    std::cout << "  [" << label << "]\n";
    if (actual.size() != 12 || expected.size() != 12) {
        std::cerr << "    FAIL: expected 12 features, got actual=" << actual.size()
                  << " expected=" << expected.size() << "\n";
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < 12; ++i) {
        float diff = std::fabs(actual[i] - expected[i]);
        bool pass = approx_equal(actual[i], expected[i], PARITY_TOL);
        std::cout << "    " << std::left << std::setw(27) << kFeatureNames[i]
                  << " Expected: " << std::fixed << std::setprecision(6) << std::setw(12) << expected[i]
                  << " Actual: " << std::setw(12) << actual[i]
                  << " Diff: " << std::scientific << std::setprecision(2) << diff
                  << (pass ? "  [PASS]\n" : "  [FAIL]\n");
        if (!pass) ok = false;
    }
    return ok;
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

    // ── Part C: Multi-session first-window parity (real fixtures) ──
    // Each expected vector is the FIRST row (smallest window_start_seq) for that
    // source_file in data/processed/features.csv — i.e. genuine Python-pipeline
    // output, transcribed at full precision, NOT hand-computed. The C++ extractor
    // replays the raw JSONL and must reproduce it within PARITY_TOL (1e-3).
    // Coverage added here: a 2nd human session, a zero-delay injected session, an
    // EVASION-family session, and a REAL multi-device session (vsh3).
    std::cout << "[Part C] Multi-session first-window parity (window=25) ...\n";
    struct SessionCase { const char* file; const char* note; std::vector<float> expected; };
    const std::vector<SessionCase> cases = {
        // human, 2nd subject
        {"vik1.jsonl", "human / 2nd subject", {
            151.579608f, 98.801206f, 0.651811f, 0.0f, 617.272568f, 583.166436f,
            0.944747f, 2.0f, 0.04f, 0.08f, 1.680405f, 1.0f}},
        // injected, zero-delay burst
        {"inj_01_zerodelay_01.jsonl", "injected / zero-delay", {
            1.367536f, 4.234755f, 3.096632f, 0.0f, 81.388546f, 232.359571f,
            2.854942f, 0.0f, 0.0f, 0.0f, 21.209842f, 1.0f}},
        // injected, EVASION family (the hard case from §3 of the audit)
        {"inj_18_evasion_payload_01.jsonl", "injected / EVASION family", {
            71.822960f, 60.018854f, 0.835650f, 0.0f, 171.079900f, 240.146784f,
            1.403711f, 0.0f, 0.0f, 0.0f, 5.128922f, 1.0f}},
        // REAL multi-device session: vsh3 has one stray null-device keydown at raw
        // event ~316 (never pairs, falls outside window 0), so first-window features
        // are unaffected and n_devices stays 1 — see Part D for the device-path gap.
        {"vsh3.jsonl", "human / REAL 2-device session", {
            133.961856f, 44.917057f, 0.335297f, 0.0f, 154.891279f, 359.949612f,
            2.323886f, 0.0f, 0.0f, 0.04f, 4.393999f, 1.0f}},
    };
    bool part_c_ok = true;
    for (const auto& c : cases) {
        auto feats = first_window_of(std::string("data/raw/") + c.file, 25, 1);
        if (feats.empty()) {
            std::cerr << "FAIL: no window produced for " << c.file << "\n";
            part_c_ok = false;
            continue;
        }
        if (!compare_features(std::string(c.file) + "  (" + c.note + ")", feats, c.expected)) {
            part_c_ok = false;
        }
    }
    if (!part_c_ok) {
        std::cerr << "FAIL: Part C multi-session parity mismatch!\n";
        return 1;
    }
    std::cout << ">>> Part C PASSED: C++ matches Python features.csv for all sessions!\n\n";

    // ── Part D: Non-dominant device-path handling (synthetic) ──
    // REGRESSION GUARD for audit defect #2 / backlog P1-a (train/serve skew).
    // Python build_features.clean_session() drops every event whose device_path is
    // not the session's dominant device BEFORE pairing, so a cleaned window reports
    // n_devices_in_window == 1. The C++ FeatureExtractor does NOT do this filtering,
    // so a window spanning two device paths reports n_devices_in_window == 2.
    // This test pins the CURRENT (divergent) C++ behaviour. When P1-a ports the
    // minority-device filter into C++, this expectation must flip from 2 -> 1, and
    // this assertion is what will flag that change.
    std::cout << "[Part D] Non-dominant device-path handling (synthetic) ...\n";
    {
        badbt::FeatureExtractor dev_fx(25, 1);
        for (size_t i = 0; i < 25; ++i) {
            badbt::PairedKeystroke pk;
            pk.seq = i + 1;
            pk.keydown_ts = 1'000'000'000LL + static_cast<int64_t>(i) * 100'000'000LL;
            pk.dwell_ms = 50.0;
            pk.flight_ms = 50.0;
            pk.vkey = 0x41;
            pk.device_path = (i == 24) ? "DEV2" : "DEV1"; // 24 on DEV1, 1 on DEV2
            dev_fx.push_paired(pk);
        }
        auto dev_feats = dev_fx.extract_window_features();
        std::cout << "    n_devices_in_window = " << dev_feats[11]
                  << " (C++ keeps both; Python clean_session would report 1)\n";
        if (!approx_equal(dev_feats[11], 2.0f)) {
            std::cerr << "FAIL: expected C++ to report 2 devices (current un-ported behaviour)\n";
            return 1;
        }
    }
    std::cout << ">>> Part D PASSED: documented C++/Python device-path divergence pinned.\n\n";

    // ── Part E: Duplicate-event handling (synthetic) ──
    // Real captures contain ZERO exact-duplicate events (verified during the audit),
    // so this divergence has no effect on any committed fixture today; the test uses
    // synthetic events built from the real schema to characterise it.
    // Python clean_session() runs a GLOBAL drop_duplicates(timestamp,vkey,scan,flags).
    // The C++ extractor only drops a duplicate that is IMMEDIATELY adjacent to its
    // twin. Consequence:
    //   * adjacent exact duplicate   -> dropped by C++ (matches Python)       -> 1 pair
    //   * non-adjacent exact dup pair -> kept by C++ (Python would drop it)    -> extra pair
    std::cout << "[Part E] Duplicate-event handling (synthetic) ...\n";
    auto make_ev = [](int64_t ts, uint16_t scan, uint16_t vkey, uint16_t flags) {
        badbt::InputEvent e;
        e.timestamp_ns = ts; e.vkey = vkey; e.scan_code = scan; e.flags = flags;
        e.device_path = "DEV1";
        return e;
    };
    {
        // E1: non-adjacent duplicate of a full keydown/keyup pair.
        badbt::FeatureExtractor dup_fx(25, 1);
        dup_fx.push_event(make_ev(0, 30, 0x41, 0));    // KD A
        dup_fx.push_event(make_ev(10, 30, 0x41, 1));   // KU A -> pair 1
        dup_fx.push_event(make_ev(100, 31, 0x42, 0));  // KD B
        dup_fx.push_event(make_ev(110, 31, 0x42, 1));  // KU B -> pair 2
        dup_fx.push_event(make_ev(0, 30, 0x41, 0));    // KD A duplicate (non-adjacent)
        dup_fx.push_event(make_ev(10, 30, 0x41, 1));   // KU A duplicate -> pair 3 (C++ keeps)
        std::cout << "    non-adjacent dup paired_count = " << dup_fx.paired_count()
                  << " (Python global dedup would yield 2)\n";
        if (dup_fx.paired_count() != 3) {
            std::cerr << "FAIL: expected C++ to retain non-adjacent duplicate (3 pairs)\n";
            return 1;
        }

        // E2: immediately-adjacent exact duplicate is dropped (consecutive dedup).
        badbt::FeatureExtractor adj_fx(25, 1);
        adj_fx.push_event(make_ev(0, 30, 0x41, 0));    // KD A
        adj_fx.push_event(make_ev(0, 30, 0x41, 0));    // KD A exact adjacent dup -> dropped
        adj_fx.push_event(make_ev(10, 30, 0x41, 1));   // KU A -> pair 1
        std::cout << "    adjacent dup paired_count     = " << adj_fx.paired_count()
                  << " (matches Python: 1)\n";
        if (adj_fx.paired_count() != 1) {
            std::cerr << "FAIL: expected adjacent duplicate to be deduped (1 pair)\n";
            return 1;
        }
    }
    std::cout << ">>> Part E PASSED: duplicate-event behaviour characterised.\n\n";

    std::cout << "========================================================\n";
    std::cout << "         ALL FEATURE PARITY TESTS PASSED!               \n";
    std::cout << "========================================================\n";
    return 0;
}

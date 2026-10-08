#include <iostream>
#include <vector>
#include <fstream>
#include <cassert>
#include <iomanip>
#include <nlohmann/json.hpp>

#include "../include/model_runner.h"
#include "../include/detector.h"

int main() {
    std::cout << "========================================================\n";
    std::cout << "        TEST 2: Native ONNX Inference & Replay          \n";
    std::cout << "========================================================\n";

    // ── Part A: Basic ModelRunner Inference ──
    std::cout << "\n[Part A] Testing ModelRunner ONNX Model Loading & Scoring...\n";
    const std::string model_path = "model/rf_model.onnx";

    badbt::ModelRunner runner(model_path);
    if (!runner.is_loaded()) {
        std::cerr << "FAIL: Could not load ONNX model from " << model_path << "\n";
        return 1;
    }
    std::cout << "  - Loaded model: " << runner.model_path() << " [SUCCESS]\n";

    // Feed row 0 features from adhi_01
    const std::vector<float> human_row0 = {
        110.598284f, 22.893004f, 0.2069924f, 0.0f,
        207.456812f, 217.063856f, 1.046309f, 0.0f,
        0.0f, 0.0f, 3.286752f, 1.0f
    };
    float p_inj_human = runner.predict_proba_injected(human_row0);
    std::cout << "  - Human sample P(injected): " << std::fixed << std::setprecision(4) << p_inj_human << "\n";
    assert(p_inj_human >= 0.0f && p_inj_human <= 1.0f);
    assert(p_inj_human < 0.50f); // Should be very low for human typing
    std::cout << ">>> Part A PASSED!\n\n";

    // ── Part B: Replay adhi_01.jsonl (Known Human Session) ──
    std::cout << "[Part B] Offline Replay: data/raw/adhi_01.jsonl (Human Session)...\n";
    badbt::DetectorConfig cfg_human;
    cfg_human.model_path = model_path;
    cfg_human.window_size = 25;
    cfg_human.window_step = 1;
    cfg_human.threshold = 0.95f;
    cfg_human.cooldown_seconds = 30.0;

    badbt::LiveDetector detector_human(cfg_human);
    assert(detector_human.initialize());

    size_t windows_human = detector_human.replay_jsonl("data/raw/adhi_01.jsonl");
    std::cout << "  - Events:           " << detector_human.total_events() << "\n";
    std::cout << "  - Paired keys:      " << detector_human.total_paired() << "\n";
    std::cout << "  - Windows:          " << windows_human << "\n";
    std::cout << "  - Injected windows: " << detector_human.total_injected_windows() << "\n";
    std::cout << "  - Alerts fired:     " << detector_human.total_alerts() << "\n";
    std::cout << "  - Max P(injected):  " << detector_human.max_proba_injected() << "\n";

    assert(windows_human == 669);
    assert(detector_human.total_injected_windows() == 0);
    assert(detector_human.total_alerts() == 0);
    assert(detector_human.max_proba_injected() < 0.95f);
    std::cout << ">>> Part B PASSED: Human session produced ZERO alerts!\n\n";

    // ── Part C: Replay inj_01_zerodelay_01.jsonl (Known Injected Session) ──
    std::cout << "[Part C] Offline Replay: data/raw/inj_01_zerodelay_01.jsonl (Injected Session)...\n";
    badbt::DetectorConfig cfg_inj;
    cfg_inj.model_path = model_path;
    cfg_inj.window_size = 25;
    cfg_inj.window_step = 1;
    cfg_inj.threshold = 0.95f;
    cfg_inj.cooldown_seconds = 30.0;

    badbt::LiveDetector detector_inj(cfg_inj);
    assert(detector_inj.initialize());

    std::vector<int64_t> alert_timestamps;
    detector_inj.set_detection_callback([&alert_timestamps](const badbt::DetectionResult& res) {
        if (res.alert_triggered) {
            alert_timestamps.push_back(res.timestamp_ns);
        }
    });

    size_t windows_inj = detector_inj.replay_jsonl("data/raw/inj_01_zerodelay_01.jsonl");
    std::cout << "  - Events:           " << detector_inj.total_events() << "\n";
    std::cout << "  - Paired keys:      " << detector_inj.total_paired() << "\n";
    std::cout << "  - Windows:          " << windows_inj << "\n";
    std::cout << "  - Injected windows: " << detector_inj.total_injected_windows() << "\n";
    std::cout << "  - Alerts fired:     " << detector_inj.total_alerts() << "\n";
    std::cout << "  - Max P(injected):  " << detector_inj.max_proba_injected() << "\n";

    assert(windows_inj == 855);
    assert(detector_inj.total_injected_windows() == 855);
    assert(detector_inj.total_alerts() >= 1);
    assert(detector_inj.max_proba_injected() >= 0.95f);

    // Verify cooldown between alerts
    for (size_t i = 1; i < alert_timestamps.size(); ++i) {
        double delta_sec = static_cast<double>(alert_timestamps[i] - alert_timestamps[i - 1]) / 1e9;
        std::cout << "  - Cooldown interval between alert " << i - 1 << " and " << i << ": "
                  << std::fixed << std::setprecision(2) << delta_sec << "s (must be >= 30s)\n";
        assert(delta_sec >= 30.0);
    }

    std::cout << ">>> Part C PASSED: Injected session 100% detected with cooldown verified!\n\n";

    // ── Part D: Two-Attack Sequential Replay (State Reset & Cooldown Lifecycle) ──
    std::cout << "[Part D] Two-Attack Sequential Lifecycle (Monitoring -> Alert -> Cooldown -> Reset -> Monitoring -> Alert)...\n";
    badbt::DetectorConfig cfg_multi;
    cfg_multi.model_path = model_path;
    cfg_multi.window_size = 25;
    cfg_multi.window_step = 1;
    cfg_multi.threshold = 0.95f;
    cfg_multi.cooldown_seconds = 30.0;

    badbt::LiveDetector detector_multi(cfg_multi);
    assert(detector_multi.initialize());
    assert(detector_multi.state() == badbt::DetectorState::Monitoring);
    std::cout << "  - Initial State: MONITORING [VERIFIED]\n";

    // Load first 100 raw events (approx 50 paired keystrokes) from inj_01
    std::ifstream multi_file("data/raw/inj_01_zerodelay_01.jsonl");
    std::vector<badbt::InputEvent> base_events;
    std::string m_line;
    while (std::getline(multi_file, m_line) && base_events.size() < 100) {
        if (m_line.empty()) continue;
        auto j = nlohmann::json::parse(m_line);
        badbt::InputEvent ev;
        ev.timestamp_ns = j.value("timestamp_ns", 0LL);
        ev.vkey = static_cast<uint16_t>(j.value("vkey", 0));
        ev.scan_code = static_cast<uint16_t>(j.value("scan_code", 0));
        ev.flags = static_cast<uint16_t>(j.value("flags", 0));
        ev.is_autorepeat = j.value("is_autorepeat", false);
        ev.device_path = j.value("device_path", "");
        base_events.push_back(ev);
    }
    assert(base_events.size() == 100);

    std::vector<int64_t> multi_alerts;
    detector_multi.set_detection_callback([&multi_alerts](const badbt::DetectionResult& res) {
        if (res.alert_triggered) {
            multi_alerts.push_back(res.timestamp_ns);
        }
    });

    // 1. Play Attack 1
    std::cout << "  - Feeding Attack 1 (100 raw events)...\n";
    for (const auto& ev : base_events) {
        detector_multi.process_event(ev);
    }
    std::cout << "  - After Attack 1: Total alerts = " << detector_multi.total_alerts() << "\n";
    assert(detector_multi.total_alerts() == 1);
    assert(detector_multi.state() == badbt::DetectorState::Cooldown);
    std::cout << "  - State after Alert 1: COOLDOWN [VERIFIED]\n";

    // 2. Prepare Attack 2: timestamps shifted by +35.0 seconds
    int64_t shift_ns = 35LL * 1'000'000'000LL;
    int64_t t_offset = (base_events.back().timestamp_ns + shift_ns) - base_events.front().timestamp_ns;

    std::cout << "  - Feeding Attack 2 (100 raw events shifted by +35.0s)...\n";
    for (size_t i = 0; i < base_events.size(); ++i) {
        badbt::InputEvent ev = base_events[i];
        ev.timestamp_ns += t_offset;
        detector_multi.process_event(ev);
    }

    std::cout << "  - After Attack 2: Total alerts = " << detector_multi.total_alerts() << "\n";
    assert(detector_multi.total_alerts() == 2);
    assert(detector_multi.state() == badbt::DetectorState::Cooldown);

    double alert_diff_sec = static_cast<double>(multi_alerts[1] - multi_alerts[0]) / 1e9;
    std::cout << "  - Time between Alert 1 and Alert 2: " << std::fixed << std::setprecision(2)
              << alert_diff_sec << "s (cooldown >= 30s verified)\n";
    assert(alert_diff_sec >= 30.0);

    // 3. Test explicit check_cooldown_expiry
    int64_t future_ts = multi_alerts[1] + 31LL * 1'000'000'000LL;
    bool expired = detector_multi.check_cooldown_expiry(future_ts);
    assert(expired == true);
    assert(detector_multi.state() == badbt::DetectorState::Monitoring);
    std::cout << "  - Post-cooldown State: MONITORING [VERIFIED]\n";
    std::cout << ">>> Part D PASSED: Full lifecycle Monitoring -> Alert -> Cooldown -> Reset -> Monitoring -> Alert verified!\n\n";

    std::cout << "========================================================\n";
    std::cout << "       ALL ONNX INFERENCE & REPLAY TESTS PASSED!        \n";
    std::cout << "========================================================\n";
    return 0;
}

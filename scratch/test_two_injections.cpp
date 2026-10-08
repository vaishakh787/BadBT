#include <iostream>
#include <vector>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include "detector/include/detector.h"

int main() {
    badbt::DetectorConfig cfg;
    cfg.model_path = "model/rf_model.onnx";
    cfg.window_size = 25;
    cfg.window_step = 1;
    cfg.threshold = 0.95f;
    cfg.cooldown_seconds = 30.0;

    badbt::LiveDetector detector(cfg);
    if (!detector.initialize()) {
        std::cerr << "Failed to init detector\n";
        return 1;
    }

    std::vector<int64_t> alert_timestamps;
    detector.set_detection_callback([&alert_timestamps](const badbt::DetectionResult& res) {
        if (res.alert_triggered) {
            std::cout << ">>> ALERT fired at window " << res.window_seq
                      << ", ts=" << res.timestamp_ns
                      << ", P(inj)=" << res.proba_injected << "\n";
            alert_timestamps.push_back(res.timestamp_ns);
        }
    });

    // Load events from inj_01_zerodelay_01.jsonl
    std::ifstream infile("data/raw/inj_01_zerodelay_01.jsonl");
    std::vector<badbt::InputEvent> events;
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
        events.push_back(ev);
    }
    std::cout << "Loaded " << events.size() << " events from inj_01\n";

    // Play first 100 events (Attack 1)
    std::cout << "\n--- Playing Attack 1 (100 events) ---\n";
    for (size_t i = 0; i < 100; ++i) {
        detector.process_event(events[i]);
    }
    std::cout << "Attack 1 finished. Total alerts so far: " << detector.total_alerts() << "\n";

    // Now play Attack 2: 100 events with timestamps shifted by +35 seconds
    int64_t shift_ns = 35LL * 1'000'000'000LL;
    int64_t last_t = events[99].timestamp_ns;
    int64_t t_offset = (last_t + shift_ns) - events[0].timestamp_ns;

    std::cout << "\n--- Playing Attack 2 (100 events, shifted by +35s) ---\n";
    for (size_t i = 0; i < 100; ++i) {
        badbt::InputEvent ev = events[i];
        ev.timestamp_ns += t_offset;
        detector.process_event(ev);
    }
    std::cout << "Attack 2 finished. Total alerts now: " << detector.total_alerts() << "\n";
    std::cout << "Alert timestamps recorded: " << alert_timestamps.size() << "\n";

    return 0;
}

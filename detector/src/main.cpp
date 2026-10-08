#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <iomanip>
#include <unordered_map>
#include <windows.h>

#include "detector.h"
#include "../../capture/src/raw_input.h"
#include "../../capture/src/device_info.h"

static std::atomic<bool> g_running{true};
static HWND g_capture_hwnd = nullptr;

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        g_running.store(false, std::memory_order_relaxed);
        if (g_capture_hwnd) {
            PostMessage(g_capture_hwnd, WM_CLOSE, 0, 0);
        }
        return TRUE;
    }
    return FALSE;
}

static void print_usage(const char* prog) {
    std::cout << "BadBT Live Detector & Verification Tool\n\n"
              << "Usage:\n"
              << "  " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --replay <path.jsonl>   Offline replay mode on a recorded JSONL file\n"
              << "  --live                  Live capture mode (default if --replay not given)\n"
              << "  --model <path.onnx>     Path to ONNX model (default: model/rf_model.onnx)\n"
              << "  --threshold <float>     Detection threshold (default: 0.95)\n"
              << "  --cooldown <seconds>    Alert cooldown in seconds (default: 30.0)\n"
              << "  --filter-bt             Filter for Bluetooth keyboards only (live mode)\n"
              << "  --verbose               Print information for every evaluated window\n"
              << "  --help                  Show this help message\n";
}

int main(int argc, char* argv[]) {
    badbt::DetectorConfig config;
    config.model_path = "model/rf_model.onnx";
    config.window_size = 25;
    config.window_step = 1;
    config.threshold = 0.95f;
    config.cooldown_seconds = 30.0;

    std::string replay_file;
    bool filter_bt = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--replay" && i + 1 < argc) {
            replay_file = argv[++i];
        } else if (arg == "--model" && i + 1 < argc) {
            config.model_path = argv[++i];
        } else if (arg == "--threshold" && i + 1 < argc) {
            config.threshold = std::stof(argv[++i]);
        } else if (arg == "--cooldown" && i + 1 < argc) {
            config.cooldown_seconds = std::stod(argv[++i]);
        } else if (arg == "--filter-bt") {
            filter_bt = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--live") {
            // default
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    std::cout << "========================================================\n";
    std::cout << "           BadBT Real-Time Detection Pipeline          \n";
    std::cout << "========================================================\n";
    std::cout << "Model:            " << config.model_path << "\n";
    std::cout << "Window Size:      " << config.window_size << " paired keystrokes\n";
    std::cout << "Window Step:      " << config.window_step << "\n";
    std::cout << "Threshold:        " << config.threshold << " (P(injected) >= " << config.threshold << ")\n";
    std::cout << "Cooldown:         " << config.cooldown_seconds << " seconds\n";
    std::cout << "Mode:             " << (replay_file.empty() ? "LIVE CAPTURE" : "OFFLINE REPLAY") << "\n";
    std::cout << "========================================================\n\n";

    badbt::LiveDetector detector(config);
    if (!detector.initialize()) {
        std::cerr << "Fatal: Failed to load ONNX model from " << config.model_path << "\n";
        return 1;
    }

    // Configure alert / window callback
    detector.set_detection_callback([verbose](const badbt::DetectionResult& res) {
        if (res.alert_triggered) {
            std::cout << "\n>>> [ALERT] INJECTION DETECTED! <<<\n"
                      << "    Window #" << res.window_seq << " | P(injected): "
                      << std::fixed << std::setprecision(4) << res.proba_injected
                      << " >= threshold\n"
                      << "    Device: " << (res.primary_device_path.empty() ? "(unknown)" : res.primary_device_path) << "\n"
                      << "    Timestamp: " << res.timestamp_ns << "\n"
                      << "--------------------------------------------------------\n";
        } else if (res.is_injected) {
            if (verbose) {
                std::cout << "[INJECTED - COOLDOWN] Window #" << res.window_seq
                          << " | P(injected): " << std::fixed << std::setprecision(4) << res.proba_injected << "\n";
            }
        } else if (verbose) {
            std::cout << "[HUMAN] Window #" << res.window_seq
                      << " | P(injected): " << std::fixed << std::setprecision(4) << res.proba_injected << "\n";
        }
    });

    if (!replay_file.empty()) {
        std::cout << "Replaying recorded events from: " << replay_file << "\n";
        size_t windows = detector.replay_jsonl(replay_file);

        std::cout << "\n========================================================\n";
        std::cout << "                    REPLAY RESULTS                      \n";
        std::cout << "========================================================\n";
        std::cout << "Total Events:        " << detector.total_events() << "\n";
        std::cout << "Paired Keystrokes:   " << detector.total_paired() << "\n";
        std::cout << "Windows Evaluated:   " << detector.total_windows() << "\n";
        std::cout << "Injected Windows:    " << detector.total_injected_windows() << "\n";
        std::cout << "Alerts Fired:        " << detector.total_alerts() << "\n";
        std::cout << "Max P(injected):     " << std::fixed << std::setprecision(4) << detector.max_proba_injected() << "\n";
        std::cout << "Final Verdict:       "
                  << (detector.total_alerts() > 0 ? "INJECTION DETECTED (BADBT)" : "HUMAN (NO INJECTION)")
                  << "\n";
        std::cout << "========================================================\n";
        return 0;
    }

    // ── Live Capture Mode ──
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    std::unordered_map<HANDLE, std::wstring> device_cache;
    std::unordered_map<HANDLE, bool> bt_status_cache;
    std::unordered_map<uint64_t, bool> key_down_state;

    auto key_state_id = [](HANDLE device, USHORT scan_code) -> uint64_t {
        return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(device)) << 16)
               ^ static_cast<uint64_t>(scan_code);
    };

    badbt::KeyEventCallback callback = [&](const badbt::RawKeyEvent& raw_event) {
        auto it = device_cache.find(raw_event.device);
        if (it == device_cache.end()) {
            std::wstring path = badbt::get_device_path(raw_event.device);
            device_cache[raw_event.device] = path;
            bt_status_cache[raw_event.device] = badbt::is_bluetooth_device(path);
            it = device_cache.find(raw_event.device);

            std::string path_utf8 = badbt::wstring_to_utf8(path);
            bool is_bt = bt_status_cache[raw_event.device];
            std::cout << "[Device Discovered] " << path_utf8
                      << (is_bt ? " [BLUETOOTH]" : " [USB/Other]") << "\n";
        }

        if (filter_bt && !bt_status_cache[raw_event.device]) {
            return;
        }

        uint64_t state_id = key_state_id(raw_event.device, raw_event.scan_code);
        bool is_keyup = (raw_event.flags & 0x01) != 0;
        bool is_autorepeat = false;
        if (is_keyup) {
            key_down_state[state_id] = false;
        } else {
            is_autorepeat = key_down_state[state_id];
            key_down_state[state_id] = true;
        }

        badbt::InputEvent ev;
        ev.timestamp_ns = raw_event.timestamp_ns;
        ev.vkey = raw_event.vkey;
        ev.scan_code = raw_event.scan_code;
        ev.flags = raw_event.flags;
        ev.is_autorepeat = is_autorepeat;
        ev.device_path = badbt::wstring_to_utf8(it->second);

        detector.process_event(ev);
    };

    g_capture_hwnd = badbt::create_capture_window(&callback);
    if (!g_capture_hwnd) {
        DWORD err = GetLastError();
        std::cerr << "Fatal: Failed to create capture window (Win32 error " << err << ")\n";
        return 1;
    }

    std::cout << "[Live Detector] Listening for keyboard input... (Press Ctrl+C to stop)\n";
    std::cout << "[Live Detector] Bluetooth filter: " << (filter_bt ? "ENABLED" : "DISABLED") << "\n\n";

    badbt::run_message_loop(g_capture_hwnd, g_running, 0);

    badbt::destroy_capture_window(g_capture_hwnd);
    g_capture_hwnd = nullptr;

    std::cout << "\n\n[Live Detector] Stopped.\n";
    std::cout << "Events captured:     " << detector.total_events() << "\n";
    std::cout << "Paired keystrokes:   " << detector.total_paired() << "\n";
    std::cout << "Windows evaluated:   " << detector.total_windows() << "\n";
    std::cout << "Alerts triggered:    " << detector.total_alerts() << "\n";

    return 0;
}

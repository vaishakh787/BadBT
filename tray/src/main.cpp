#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

#include "tray_app.h"

static void print_usage(const char* prog) {
    std::cout << "BadBT Windows System Tray Application\n\n"
              << "Usage:\n"
              << "  " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --model <path.onnx>        Custom ONNX model path\n"
              << "  --replay <path.jsonl>      Replay an offline capture file in tray mode\n"
              << "  --all-keyboards            Monitor all keyboards (disables Bluetooth filter)\n"
              << "  --test-notification        Trigger a test notification on startup\n"
              << "  --exit-after <seconds>     Automatically exit after N seconds (useful for automated testing)\n"
              << "  --help                     Show this help message\n";
}

int main(int argc, char* argv[]) {
    badbt::TrayConfig config;
    config.filter_bluetooth = true;
    config.threshold = 0.95f;
    config.cooldown_seconds = 30.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            config.model_path = argv[++i];
        } else if (arg == "--replay" && i + 1 < argc) {
            config.replay_file = argv[++i];
        } else if (arg == "--all-keyboards") {
            config.filter_bluetooth = false;
        } else if (arg == "--test-notification") {
            config.test_notification_on_start = true;
        } else if (arg == "--exit-after" && i + 1 < argc) {
            config.auto_exit_seconds = std::stoi(argv[++i]);
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
    std::cout << "      BadBT Keystroke Injection Shield (System Tray)    \n";
    std::cout << "========================================================\n";
    std::cout << "Status:           Initializing system tray icon...\n";
    std::cout << "Bluetooth filter: " << (config.filter_bluetooth ? "ENABLED" : "DISABLED") << "\n";
    if (!config.replay_file.empty()) {
        std::cout << "Replay Mode:      " << config.replay_file << "\n";
    }
    if (config.auto_exit_seconds > 0) {
        std::cout << "Auto-exit timer:  " << config.auto_exit_seconds << "s\n";
    }
    std::cout << "========================================================\n\n";

    badbt::TrayApp app(config);
    if (!app.initialize()) {
        std::cerr << "Fatal: Failed to initialize BadBT tray application.\n";
        return 1;
    }

    std::cout << "[BadBT Tray] Running in background system tray. Right-click icon for menu.\n";
    int ret = app.run();
    std::cout << "[BadBT Tray] Shutdown complete.\n";

    return ret;
}

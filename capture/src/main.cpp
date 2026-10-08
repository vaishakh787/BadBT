// BadBT Detection — Capture Tool Entry Point
//
// Captures raw keyboard input via the Windows Raw Input API,
// filters for Bluetooth devices, and writes JSONL to a file.
//
// Usage:
//   badbt-capture --output FILE --label human|injected --subject ID --session ID [options]
//
// See: IMPLEMENTATION_PLAN.md §3

#include <iostream>
#include <fstream>
#include <atomic>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <utility>
#include <Windows.h>

#include "cli.h"
#include "raw_input.h"
#include "device_info.h"
#include "schema.h"
#include <nlohmann/json.hpp>

// ─── Helpers ────────────────────────────────────────────────────────

static std::string handle_to_hex(HANDLE h) {
    std::ostringstream oss;
    oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(h);
    return oss.str();
}

// ─── Globals for Ctrl+C handling ────────────────────────────────────

static std::atomic<bool>  g_running{true};
static HWND               g_capture_hwnd = nullptr;

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        g_running.store(false, std::memory_order_relaxed);
        // Post WM_CLOSE to break the message loop
        if (g_capture_hwnd) {
            PostMessage(g_capture_hwnd, WM_CLOSE, 0, 0);
        }
        return TRUE;
    }
    return FALSE;
}

// ─── Main ───────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ── Parse CLI ──
    badbt::CaptureConfig config;
    if (!badbt::parse_args(argc, argv, config)) {
        return 1;
    }

    // ── Open output file ──
    std::ofstream outfile(config.output_path, std::ios::app);
    if (!outfile.is_open()) {
        std::cerr << "Error: cannot open output file: " << config.output_path << "\n";
        return 1;
    }

    // ── Setup ──
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    // Device path cache: avoid querying GetRawInputDeviceInfo on every keystroke
    std::unordered_map<HANDLE, std::wstring> device_cache;
    // Track which devices we've already determined are non-BT (skip silently)
    std::unordered_map<HANDLE, bool> bt_status_cache;

    uint64_t events_captured = 0;
    uint64_t events_filtered = 0;
    uint64_t seq_counter = 0;
    constexpr int FLUSH_INTERVAL = 100;  // Flush output every N events

    // Tracks which (device, scan_code) pairs are currently held down,
    // so a repeated keydown with no intervening keyup can be flagged
    // as autorepeat rather than a fresh press.
    std::unordered_map<uint64_t, bool> key_down_state;
    auto key_state_id = [](HANDLE device, USHORT scan_code) -> uint64_t {
        return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(device)) << 16)
               ^ static_cast<uint64_t>(scan_code);
    };

    // ── Event callback ──
    badbt::KeyEventCallback callback = [&](const badbt::RawKeyEvent& raw_event) {
        // Look up device path (cached)
        auto it = device_cache.find(raw_event.device);
        if (it == device_cache.end()) {
            std::wstring path = badbt::get_device_path(raw_event.device);
            device_cache[raw_event.device] = path;
            bt_status_cache[raw_event.device] = badbt::is_bluetooth_device(path);
            it = device_cache.find(raw_event.device);

            // Log new device discovery
            std::string path_utf8 = badbt::wstring_to_utf8(path);
            bool is_bt = bt_status_cache[raw_event.device];
            std::cerr << "[device] " << path_utf8
                      << (is_bt ? " [BLUETOOTH]" : " [non-BT]") << "\n";
        }

        // Filter: skip non-Bluetooth devices if BT filtering is enabled
        if (config.filter_bluetooth && !bt_status_cache[raw_event.device]) {
            ++events_filtered;
            return;
        }

        // Autorepeat detection: RI_KEY_MAKE (0) with the key already down
        // means this is an OS-generated repeat, not a fresh keypress.
        uint64_t state_id = key_state_id(raw_event.device, raw_event.scan_code);
        bool is_keyup = (raw_event.flags & 0x01) != 0;  // RI_KEY_BREAK
        bool is_autorepeat = false;
        if (is_keyup) {
            key_down_state[state_id] = false;
        } else {
            is_autorepeat = key_down_state[state_id];  // already true means held
            key_down_state[state_id] = true;
        }

        // Build KeyEvent
        badbt::KeyEvent event{};
        event.seq             = ++seq_counter;
        event.timestamp_ns    = raw_event.timestamp_ns;
        event.qpc_ticks        = raw_event.qpc_ticks;
        event.device_path     = badbt::wstring_to_utf8(it->second);
        event.device_handle   = handle_to_hex(raw_event.device);
        event.vkey            = raw_event.vkey;
        event.scan_code       = raw_event.scan_code;
        event.flags           = raw_event.flags;
        event.message          = raw_event.message;
        event.is_autorepeat    = is_autorepeat;
        event.label           = config.label;
        event.subject_id      = config.subject_id;
        event.session_id      = config.session_id;
        event.payload_variant = config.payload_variant;

        // Serialize to JSONL (one JSON object per line)
        nlohmann::json j = event;
        outfile << j.dump() << '\n';
        ++events_captured;

        // Periodic flush
        if (events_captured % FLUSH_INTERVAL == 0) {
            outfile.flush();
            std::cerr << "\r[capture] " << events_captured << " events written";
        }
    };

    // ── Create capture window and register raw input ──
    g_capture_hwnd = badbt::create_capture_window(&callback);
    if (!g_capture_hwnd) {
        DWORD err = GetLastError();
        std::cerr << "Error: failed to create capture window (Win32 error " << err << ")\n";
        return 1;
    }

    // ── Status ──
    std::cerr << "[badbt-capture] Listening for keyboard input...\n";
    std::cerr << "[badbt-capture] Output: " << config.output_path << "\n";
    std::cerr << "[badbt-capture] Label: "
              << (config.label == badbt::Label::Human ? "human" : "injected") << "\n";
    std::cerr << "[badbt-capture] BT filter: "
              << (config.filter_bluetooth ? "ON" : "OFF") << "\n";
    if (config.duration_secs > 0) {
        std::cerr << "[badbt-capture] Duration: " << config.duration_secs << "s\n";
    } else {
        std::cerr << "[badbt-capture] Duration: unlimited (Ctrl+C to stop)\n";
    }
    std::cerr << "\n";

    // ── Run capture loop ──
    badbt::run_message_loop(g_capture_hwnd, g_running, config.duration_secs);

    // ── Cleanup ──
    badbt::destroy_capture_window(g_capture_hwnd);
    g_capture_hwnd = nullptr;

    outfile.flush();
    outfile.close();

    std::cerr << "\n\n[badbt-capture] Done.\n";
    std::cerr << "[badbt-capture] Events captured: " << events_captured << "\n";
    std::cerr << "[badbt-capture] Events filtered (non-BT): " << events_filtered << "\n";

    return 0;
}

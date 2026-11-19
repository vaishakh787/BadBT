#include <iostream>
#include <vector>
#include <fstream>
#include <cassert>
#include <iomanip>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>
#include "../include/tray_app.h"
#include "../include/notification.h"
#include "../../detector/include/path_utils.h"

// Helper to load raw events from a JSONL file
static std::vector<badbt::InputEvent> load_events(const std::string& path, size_t max_events = 0) {
    std::vector<badbt::InputEvent> events;
    std::ifstream infile(path);
    if (!infile.is_open()) {
        std::cerr << "Error: Cannot open " << path << std::endl;
        return events;
    }
    std::string line;
    while (std::getline(infile, line)) {
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            badbt::InputEvent ev;
            ev.timestamp_ns = j.value("timestamp_ns", 0LL);
            ev.vkey = static_cast<uint16_t>(j.value("vkey", 0));
            ev.scan_code = static_cast<uint16_t>(j.value("scan_code", 0));
            ev.flags = static_cast<uint16_t>(j.value("flags", 0));
            ev.is_autorepeat = j.value("is_autorepeat", false);
            ev.device_path = j.value("device_path", "");
            events.push_back(ev);
            if (max_events > 0 && events.size() >= max_events) break;
        } catch (...) {}
    }
    return events;
}

static void pump_ui_messages(HWND hwnd) {
    MSG msg;
    while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// Simulates a low-level keyboard event passed directly into TrayApp's hook callback
static LRESULT simulate_hook_event(DWORD vkCode, WPARAM msg_type) {
    KBDLLHOOKSTRUCT kbd{};
    kbd.vkCode = vkCode;
    kbd.scanCode = static_cast<DWORD>(MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC));
    kbd.flags = (msg_type == WM_KEYUP || msg_type == WM_SYSKEYUP) ? LLKHF_UP : 0;
    if (msg_type == WM_SYSKEYDOWN || msg_type == WM_SYSKEYUP) {
        kbd.flags |= LLKHF_ALTDOWN;
    }
    kbd.time = 0;
    kbd.dwExtraInfo = 0;
    return badbt::TrayApp::low_level_keyboard_proc(HC_ACTION, msg_type, reinterpret_cast<LPARAM>(&kbd));
}

int main() {
    // Unbuffered output for clear, immediate test logs
    setvbuf(stdout, NULL, _IONBF, 0);

    std::cout << "========================================================\n";
    std::cout << "  TEST: BadBT UI, Lifecycle & Post-Detection Blocking   \n";
    std::cout << "========================================================\n";

    // Use a dedicated temp blocklist file so the test is deterministic and never touches
    // the user's real %APPDATA%\BadBT\blocklist.txt. Start from a clean slate.
    const char* kBlocklistPath = "test_tray_blocklist_tmp.txt";
    std::remove(kBlocklistPath);

    // Configure TrayApp for deterministic synchronous testing
    badbt::TrayConfig config;
    config.auto_exit_seconds = 0;
    config.filter_bluetooth = false;
    config.start_minimized = true;
    config.enable_capture_thread = false; // Synchronous, deterministic testing
    config.enable_keyboard_hook = true;   // Test WH_KEYBOARD_LL hook registration
    config.blocklist_path = kBlocklistPath;
    config.disconnect_on_detect = false;  // tests 1-11 cover the legacy block-until-Reset path

    badbt::TrayApp app(config);
    bool init_ok = app.initialize();
    std::cout << "  - TrayApp initialization:       " << (init_ok ? "SUCCESS" : "FAILED") << "\n";
    assert(init_ok);

    // Initial Controls & UI Verification
    std::cout << "\n[Pre-Test] Verifying Basic UI Controls & Notification API...\n";
    assert(app.is_detection_running() == true);
    assert(app.is_blocking_active() == false);
    assert(app.current_status_string() == L"Monitoring");
    assert(app.last_alert_time_string() == L"None");
    assert(app.blocklist_size() == 0);

    SendMessageW(app.hwnd(), WM_COMMAND, MAKEWPARAM(badbt::IDC_BTN_TEST_ALERT, 0), 0);
    bool notif_ok = badbt::show_tray_notification(
        app.hwnd(), badbt::TRAY_ICON_UID, L"BadBT Test", L"Deterministic test notification.", NIIF_INFO
    );
    assert(notif_ok);
    std::cout << "  - Basic UI & notification API:  PASS\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 1: Monitoring + normal keyboard -> blocking false -> hook allows key
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 1] Monitoring + normal keyboard -> blocking false -> hook allows key...\n";
    assert(app.is_detection_running() == true);
    assert(app.is_blocking_active() == false);

    auto human_events = load_events("data/raw/adhi_01.jsonl");
    assert(!human_events.empty());
    std::cout << "  - Replaying " << human_events.size() << " human events...\n";

    for (const auto& ev : human_events) {
        app.detector()->process_event(ev);
    }
    pump_ui_messages(app.hwnd());

    assert(app.total_alerts() == 0);
    assert(app.is_blocking_active() == false);
    assert(app.current_status_string() == L"Monitoring");

    // Hook must allow keys when blocking is inactive (returns 0 via CallNextHookEx)
    LRESULT t1_down = simulate_hook_event('A', WM_KEYDOWN);
    LRESULT t1_up   = simulate_hook_event('A', WM_KEYUP);
    assert(t1_down == 0);
    assert(t1_up == 0);
    std::cout << ">>> TEST 1 PASSED: Normal typing produces 0 alerts, blocking is false, keys allowed.\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 2: Injection replay reaches detection -> device blocklisted -> blocking becomes true
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 2] Injection replay -> Alert -> device blocklisted -> blocking becomes true...\n";
    auto inj_events = load_events("data/raw/inj_01_zerodelay_01.jsonl", 100); // 100 events = 50 paired keys
    assert(inj_events.size() == 100);

    for (const auto& ev : inj_events) {
        app.detector()->process_event(ev);
    }
    pump_ui_messages(app.hwnd());

    std::cout << "  - Attack 1 alerts triggered:    " << app.total_alerts() << "\n";
    std::cout << "  - Blocklisted devices:          " << app.blocklist_size() << "\n";
    assert(app.total_alerts() == 1);
    assert(app.current_status_string() == L"Blocked");
    assert(app.is_blocking_active() == true);
    assert(app.blocklist_size() == 1); // offending device recorded + persisted
    std::cout << ">>> TEST 2 PASSED: Alert triggered, device added to blocklist, blocking activated.\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 3: While blocking: normal KEYDOWN -> hook returns 1
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 3] While blocking: normal KEYDOWN -> hook returns 1...\n";
    assert(app.is_blocking_active() == true);

    LRESULT t3_a      = simulate_hook_event('A', WM_KEYDOWN);
    LRESULT t3_return = simulate_hook_event(VK_RETURN, WM_KEYDOWN);
    LRESULT t3_space  = simulate_hook_event(VK_SPACE, WM_KEYDOWN);
    assert(t3_a == 1);
    assert(t3_return == 1);
    assert(t3_space == 1);
    std::cout << ">>> TEST 3 PASSED: All normal KEYDOWN events return 1 (blocked).\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 4: While blocking: normal SYSKEYDOWN -> hook returns 1
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 4] While blocking: normal SYSKEYDOWN -> hook returns 1...\n";
    assert(app.is_blocking_active() == true);

    LRESULT t4_a  = simulate_hook_event('A', WM_SYSKEYDOWN);
    LRESULT t4_f4 = simulate_hook_event(VK_F4, WM_SYSKEYDOWN);
    assert(t4_a == 1);
    assert(t4_f4 == 1);
    std::cout << ">>> TEST 4 PASSED: Normal SYSKEYDOWN events return 1 (blocked).\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 5: While blocking: normal KEYUP -> hook returns 1
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 5] While blocking: normal KEYUP -> hook returns 1...\n";
    assert(app.is_blocking_active() == true);

    LRESULT t5_a      = simulate_hook_event('A', WM_KEYUP);
    LRESULT t5_return = simulate_hook_event(VK_RETURN, WM_KEYUP);
    assert(t5_a == 1);
    assert(t5_return == 1);
    std::cout << ">>> TEST 5 PASSED: Normal KEYUP events return 1 (blocked).\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 6: While blocking: modifier KEYUP -> hook returns 0 (CallNextHookEx path)
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 6] While blocking: modifier KEYUP -> hook returns 0 (CallNextHookEx path)...\n";
    assert(app.is_blocking_active() == true);

    // Shift
    assert(simulate_hook_event(VK_SHIFT, WM_KEYUP) == 0);
    assert(simulate_hook_event(VK_LSHIFT, WM_KEYUP) == 0);
    assert(simulate_hook_event(VK_RSHIFT, WM_KEYUP) == 0);
    std::cout << "  - Shift keyups allowed:         PASS\n";

    // Ctrl
    assert(simulate_hook_event(VK_CONTROL, WM_KEYUP) == 0);
    assert(simulate_hook_event(VK_LCONTROL, WM_KEYUP) == 0);
    assert(simulate_hook_event(VK_RCONTROL, WM_KEYUP) == 0);
    std::cout << "  - Ctrl keyups allowed:          PASS\n";

    // Alt (both WM_KEYUP and WM_SYSKEYUP)
    assert(simulate_hook_event(VK_MENU, WM_KEYUP) == 0);
    assert(simulate_hook_event(VK_LMENU, WM_KEYUP) == 0);
    assert(simulate_hook_event(VK_RMENU, WM_KEYUP) == 0);
    assert(simulate_hook_event(VK_MENU, WM_SYSKEYUP) == 0);
    assert(simulate_hook_event(VK_LMENU, WM_SYSKEYUP) == 0);
    assert(simulate_hook_event(VK_RMENU, WM_SYSKEYUP) == 0);
    std::cout << "  - Alt keyups allowed:           PASS\n";

    // Win
    assert(simulate_hook_event(VK_LWIN, WM_KEYUP) == 0);
    assert(simulate_hook_event(VK_RWIN, WM_KEYUP) == 0);
    std::cout << "  - Win keyups allowed:           PASS\n";

    std::cout << ">>> TEST 6 PASSED: All modifier KEYUP events passed through safely (prevent stuck modifiers).\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 7: Reset clears the blocklist -> Monitoring -> blocking false -> input allowed
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 7] Reset blocklist -> Monitoring -> blocking false -> input allowed...\n";
    SendMessageW(app.hwnd(), WM_COMMAND, MAKEWPARAM(badbt::IDC_BTN_RESET, 0), 0);

    assert(app.detector()->state() == badbt::DetectorState::Monitoring);
    assert(app.current_status_string() == L"Monitoring");
    assert(app.is_blocking_active() == false);
    assert(app.blocklist_size() == 0); // Reset cleared the blocklist

    // Keyboard input must be allowed now
    assert(simulate_hook_event('A', WM_KEYDOWN) == 0);
    assert(simulate_hook_event('A', WM_KEYUP) == 0);
    std::cout << ">>> TEST 7 PASSED: Reset cleared the blocklist, blocking disabled, input allowed again.\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 8: Second attack after Reset -> detection occurs again -> blocking becomes true again
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 8] Second attack after Reset -> detection occurs -> blocking becomes true again...\n";
    int64_t shift_ns = 35LL * 1'000'000'000LL;
    int64_t t_offset = (inj_events.back().timestamp_ns + shift_ns) - inj_events.front().timestamp_ns;

    for (size_t i = 0; i < inj_events.size(); ++i) {
        badbt::InputEvent ev = inj_events[i];
        ev.timestamp_ns += t_offset;
        app.detector()->process_event(ev);
    }
    pump_ui_messages(app.hwnd());

    std::cout << "  - Total alerts after Attack 2:  " << app.total_alerts() << "\n";
    assert(app.total_alerts() == 2);
    assert(app.current_status_string() == L"Blocked");
    assert(app.is_blocking_active() == true);
    assert(app.blocklist_size() == 1); // same device re-added (deduped to 1)

    // Verify blocking is active again
    assert(simulate_hook_event('B', WM_KEYDOWN) == 1);
    std::cout << ">>> TEST 8 PASSED: Second attack successfully re-armed blocking.\n";

    // Reset before next test
    SendMessageW(app.hwnd(), WM_COMMAND, MAKEWPARAM(badbt::IDC_BTN_RESET, 0), 0);
    assert(app.is_blocking_active() == false);
    assert(app.blocklist_size() == 0);

    // ─────────────────────────────────────────────────────────────────
    // TEST 9: Protection disabled -> blocking false -> hook allows keyboard input even in blocking condition
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 9] Protection disabled -> blocking false -> hook allows keyboard input...\n";
    // Toggle protection OFF
    SendMessageW(app.hwnd(), WM_COMMAND, MAKEWPARAM(badbt::IDC_BTN_TOGGLE, 0), 0);
    assert(app.is_detection_running() == false);
    assert(app.current_status_string() == L"Disabled");
    assert(app.is_blocking_active() == false);

    // Even if blocking state was hypothetically set, disabled protection forces hook pass-through
    assert(simulate_hook_event('A', WM_KEYDOWN) == 0);
    assert(simulate_hook_event('A', WM_KEYUP) == 0);

    // Replay attack events while disabled: the real capture loop gates feeding on
    // detection_enabled_, so mirror that here. Expect 0 new alerts, blocking stays false.
    for (const auto& ev : inj_events) {
        if (app.is_detection_running()) {
            app.detector()->process_event(ev);
        }
    }
    pump_ui_messages(app.hwnd());
    assert(app.total_alerts() == 2); // Unchanged
    assert(app.is_blocking_active() == false);
    assert(simulate_hook_event('A', WM_KEYDOWN) == 0);
    std::cout << ">>> TEST 9 PASSED: Protection disabled never blocks keyboard input.\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 10: Protection re-enabled -> normal detection resumes -> attack activates blocking
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 10] Protection re-enabled -> detection resumes -> attack activates blocking...\n";
    // Toggle protection ON
    SendMessageW(app.hwnd(), WM_COMMAND, MAKEWPARAM(badbt::IDC_BTN_TOGGLE, 0), 0);
    assert(app.is_detection_running() == true);
    assert(app.detector()->state() == badbt::DetectorState::Monitoring);
    assert(app.current_status_string() == L"Monitoring");
    assert(app.is_blocking_active() == false);

    // Feed Attack 3 shifted by +70s
    int64_t shift3_ns = 70LL * 1'000'000'000LL;
    int64_t t_offset3 = (inj_events.back().timestamp_ns + shift3_ns) - inj_events.front().timestamp_ns;
    for (size_t i = 0; i < inj_events.size(); ++i) {
        badbt::InputEvent ev = inj_events[i];
        ev.timestamp_ns += t_offset3;
        app.detector()->process_event(ev);
    }
    pump_ui_messages(app.hwnd());

    std::cout << "  - Total alerts after Attack 3:  " << app.total_alerts() << "\n";
    assert(app.total_alerts() == 3);
    assert(app.is_blocking_active() == true);
    assert(app.blocklist_size() == 1);
    assert(simulate_hook_event('C', WM_KEYDOWN) == 1);
    std::cout << ">>> TEST 10 PASSED: Protection re-enabled cleanly resumes monitoring and blocking.\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 12: Disconnect-on-detect -> device disconnected -> block lifts automatically;
    //          a failed disconnect must NOT leave the keyboard blocked
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 12] Disconnect-on-detect: block lifts after success AND after failure...\n";
    SendMessageW(app.hwnd(), WM_COMMAND, MAKEWPARAM(badbt::IDC_BTN_RESET, 0), 0);
    assert(app.is_blocking_active() == false);
    assert(app.blocklist_size() == 0);

    std::atomic<int>  fake_calls{0};
    std::atomic<bool> fake_succeeds{true};
    app.set_disconnect_fn([&](const std::string&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        badbt::DisconnectResult r;
        r.success = fake_succeeds.load();
        r.detail = r.success ? "fake disconnect ok" : "fake disconnect refused";
        ++fake_calls;
        return r;
    });
    app.set_disconnect_on_detect(true);

    auto pump_for = [&](int ms) {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            pump_ui_messages(app.hwnd());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    // 12a: successful disconnect
    const size_t alerts_before = app.total_alerts();
    int64_t shift4_ns = 105LL * 1'000'000'000LL;
    int64_t t_offset4 = (inj_events.back().timestamp_ns + shift4_ns) - inj_events.front().timestamp_ns;
    for (size_t i = 0; i < inj_events.size(); ++i) {
        badbt::InputEvent ev = inj_events[i];
        ev.timestamp_ns += t_offset4;
        app.detector()->process_event(ev);
    }
    assert(app.total_alerts() == alerts_before + 1);
    assert(app.is_blocking_active() == true);          // safety gate while disconnecting
    assert(app.blocklist_size() == 1);

    for (int i = 0; i < 300 && app.total_disconnects() == 0; ++i) {
        pump_for(10);
    }
    assert(fake_calls.load() == 1);
    assert(app.total_disconnects() == 1);
    assert(app.is_blocking_active() == false);         // lifted automatically, no Reset needed
    assert(app.current_status_string() == L"Monitoring");
    assert(app.blocklist_size() == 1);                 // device stays on the blocklist
    assert(simulate_hook_event('D', WM_KEYDOWN) == 0);
    std::cout << "  - 12a success: block lifted automatically, device stays blocklisted\n";

    // 12b: a failed disconnect must not lock the user out of their keyboard
    SendMessageW(app.hwnd(), WM_COMMAND, MAKEWPARAM(badbt::IDC_BTN_RESET, 0), 0);
    fake_succeeds.store(false);
    int64_t shift5_ns = 140LL * 1'000'000'000LL;
    int64_t t_offset5 = (inj_events.back().timestamp_ns + shift5_ns) - inj_events.front().timestamp_ns;
    for (size_t i = 0; i < inj_events.size(); ++i) {
        badbt::InputEvent ev = inj_events[i];
        ev.timestamp_ns += t_offset5;
        app.detector()->process_event(ev);
    }
    for (int i = 0; i < 300 && fake_calls.load() < 2; ++i) {
        pump_for(10);
    }
    pump_for(300); // let the failure outcome reach the UI thread
    assert(fake_calls.load() == 2);
    assert(app.total_disconnects() == 1);              // unchanged: this one failed
    assert(app.is_blocking_active() == false);         // gate released even though it failed
    assert(app.current_status_string() == L"Monitoring");
    assert(simulate_hook_event('E', WM_KEYDOWN) == 0);
    assert(app.blocklist_size() == 1);                 // device stays blocklisted for retry
    std::cout << "  - 12b failure: input is NOT left blocked; device stays blocklisted\n";

    SendMessageW(app.hwnd(), WM_COMMAND, MAKEWPARAM(badbt::IDC_BTN_RESET, 0), 0);
    app.set_disconnect_on_detect(false);
    assert(app.is_blocking_active() == false);
    std::cout << ">>> TEST 12 PASSED: Gate lifts after a successful AND after a failed disconnect.\n";

    // ─────────────────────────────────────────────────────────────────
    // TEST 11: Shutdown -> hook removed -> blocking false -> no dangling callback/state
    // ─────────────────────────────────────────────────────────────────
    std::cout << "\n[TEST 11] Shutdown -> hook removed -> blocking false -> no dangling callback/state...\n";
    HHOOK pre_shutdown_hook = app.keyboard_hook();
    std::cout << "  - Pre-shutdown hook handle:     " << (pre_shutdown_hook ? "INSTALLED" : "NULL") << "\n";

    app.shutdown();
    assert(app.is_blocking_active() == false);
    assert(app.keyboard_hook() == nullptr);

    // Invocations after shutdown must safely return CallNextHookEx without crashing or dangling references
    LRESULT post_shutdown_down = simulate_hook_event('A', WM_KEYDOWN);
    LRESULT post_shutdown_up   = simulate_hook_event('A', WM_KEYUP);
    assert(post_shutdown_down == 0);
    assert(post_shutdown_up == 0);
    std::cout << "  - Post-shutdown hook call:      SAFE (returned 0, no crash)\n";
    std::cout << ">>> TEST 11 PASSED: Shutdown cleanly removed hook, cleared state, and neutralized callbacks.\n";

    std::remove(kBlocklistPath); // clean up the temp blocklist file

    std::cout << "\n========================================================\n";
    std::cout << "      ALL 12 BLOCKING & LIFECYCLE TESTS 100% PASSED!    \n";
    std::cout << "========================================================\n";
    return 0;
}

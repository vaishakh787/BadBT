#include "tray_app.h"
#include "notification.h"
#include "../../detector/include/path_utils.h"
#include "../../capture/src/raw_input.h"
#include "../../capture/src/device_info.h"

#include <iostream>
#include <unordered_map>
#include <cwchar>

namespace badbt {

static const wchar_t* TRAY_WINDOW_CLASS = L"BadBTTrayWindowClass";

std::atomic<TrayApp*> TrayApp::s_instance_{nullptr};

static bool is_modifier_key(DWORD vkCode) {
    switch (vkCode) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK TrayApp::low_level_keyboard_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    TrayApp* app = s_instance_.load(std::memory_order_acquire);
    if (!app || !app->is_detection_running() || !app->is_blocking_active()) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // Blocking is active: inspect key message
    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        return 1;
    }

    if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        if (lParam != 0) {
            auto* kbd = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
            if (is_modifier_key(kbd->vkCode)) {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
        }
        return 1;
    }

    return 1;
}

void TrayApp::install_keyboard_hook() {
    if (!config_.enable_keyboard_hook) {
        return;
    }
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    hook_ = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        low_level_keyboard_proc,
        hInstance,
        0
    );
    if (!hook_) {
        DWORD err = GetLastError();
        std::cerr << "[TrayApp] Warning: Failed to install WH_KEYBOARD_LL hook (Error " << err << ").\n";
    }
}

void TrayApp::uninstall_keyboard_hook() {
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
}

TrayApp::TrayApp(const TrayConfig& config)
    : config_(config) {}

TrayApp::~TrayApp() {
    shutdown();
}

bool TrayApp::initialize() {
    // Ensure thread is attached to the user's interactive desktop (WinSta0\Default)
    HDESK hDesk = OpenDesktopW(L"Default", 0, FALSE, GENERIC_ALL);
    if (hDesk) {
        SetThreadDesktop(hDesk);
    }

    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // 1. Load icon (IDI_SHIELD provides official Windows Security Shield icon)
    icon_ = LoadIconW(nullptr, IDI_SHIELD);
    if (!icon_) {
        icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    }

    // 2. Register main desktop window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wndproc;
    wc.hInstance      = hInstance;
    wc.lpszClassName  = TRAY_WINDOW_CLASS;
    wc.hIcon          = icon_;
    wc.hIconSm        = icon_;
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground  = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassExW(&wc);

    // 3. Create desktop UI window centered on screen
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int win_w = 440;
    int win_h = 360;
    int win_x = (screen_w - win_w) / 2;
    int win_y = (screen_h - win_h) / 2;

    hwnd_ = CreateWindowExW(
        0, TRAY_WINDOW_CLASS, L"BadBT - Keystroke Injection Shield",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        win_x, win_y, win_w, win_h,
        nullptr, nullptr, hInstance, this
    );
    if (!hwnd_) {
        std::cerr << "[TrayApp] Error: Failed to create BadBT desktop window.\n";
        return false;
    }

    // 4. Create typography fonts and resources
    hfont_title_ = CreateFontW(-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    hfont_bold_ = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    hfont_normal_ = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    hfont_status_ = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    hbr_bg_ = GetSysColorBrush(COLOR_BTNFACE);

    // 5. Create native Win32 child UI controls
    hwnd_title_ = CreateWindowExW(0, L"STATIC", L"BadBT Keystroke Injection Shield",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 24, 14, 380, 24, hwnd_, nullptr, hInstance, nullptr);
    hwnd_subtitle_ = CreateWindowExW(0, L"STATIC", L"Native Real-Time Bluetooth Keystroke Injection Defense",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 24, 38, 380, 18, hwnd_, nullptr, hInstance, nullptr);

    hwnd_group_ = CreateWindowExW(0, L"BUTTON", L" Protection Status ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 20, 60, 390, 178, hwnd_, nullptr, hInstance, nullptr);

    hwnd_status_lbl_ = CreateWindowExW(0, L"STATIC", L"Protection Status:",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 36, 84, 124, 20, hwnd_, nullptr, hInstance, nullptr);
    hwnd_status_val_ = CreateWindowExW(0, L"STATIC", L"Monitoring",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 165, 82, 230, 22, hwnd_, reinterpret_cast<HMENU>(IDC_STATUS_VALUE), hInstance, nullptr);

    hwnd_state_lbl_ = CreateWindowExW(0, L"STATIC", L"Current State:",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 36, 112, 124, 20, hwnd_, nullptr, hInstance, nullptr);
    hwnd_state_detail_ = CreateWindowExW(0, L"STATIC", L"Active - Monitoring Bluetooth keystrokes in real-time.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 165, 112, 230, 36, hwnd_, reinterpret_cast<HMENU>(IDC_STATE_DETAIL), hInstance, nullptr);

    hwnd_last_detect_lbl_ = CreateWindowExW(0, L"STATIC", L"Last Detection:",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 36, 154, 124, 20, hwnd_, nullptr, hInstance, nullptr);
    hwnd_last_detect_val_ = CreateWindowExW(0, L"STATIC", L"None",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 165, 154, 230, 20, hwnd_, reinterpret_cast<HMENU>(IDC_LAST_DETECT_VALUE), hInstance, nullptr);

    hwnd_policy_lbl_ = CreateWindowExW(0, L"STATIC", L"Detection Policy:",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 36, 182, 124, 20, hwnd_, nullptr, hInstance, nullptr);
    hwnd_policy_val_ = CreateWindowExW(0, L"STATIC", L"25 keys | P >= 0.95 | 30s Cooldown",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 165, 182, 230, 20, hwnd_, nullptr, hInstance, nullptr);

    hwnd_btn_toggle_ = CreateWindowExW(0, L"BUTTON", L"Disable Protection",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 20, 248, 146, 32, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_TOGGLE), hInstance, nullptr);
    hwnd_btn_test_alert_ = CreateWindowExW(0, L"BUTTON", L"Test Alert",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 174, 248, 110, 32, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_TEST_ALERT), hInstance, nullptr);
    hwnd_btn_exit_ = CreateWindowExW(0, L"BUTTON", L"Exit",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 292, 248, 118, 32, hwnd_, reinterpret_cast<HMENU>(IDC_BTN_EXIT), hInstance, nullptr);

    hwnd_footer_ = CreateWindowExW(0, L"STATIC", L"BadBT v1.0 • System Tray Active",
        WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 290, 390, 16, hwnd_, nullptr, hInstance, nullptr);

    // Apply fonts
    SendMessageW(hwnd_title_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_title_), TRUE);
    SendMessageW(hwnd_subtitle_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_normal_), TRUE);
    SendMessageW(hwnd_group_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_bold_), TRUE);
    SendMessageW(hwnd_status_lbl_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_bold_), TRUE);
    SendMessageW(hwnd_status_val_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_status_), TRUE);
    SendMessageW(hwnd_state_lbl_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_bold_), TRUE);
    SendMessageW(hwnd_state_detail_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_normal_), TRUE);
    SendMessageW(hwnd_last_detect_lbl_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_bold_), TRUE);
    SendMessageW(hwnd_last_detect_val_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_normal_), TRUE);
    SendMessageW(hwnd_policy_lbl_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_normal_), TRUE);
    SendMessageW(hwnd_policy_val_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_normal_), TRUE);
    SendMessageW(hwnd_btn_toggle_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_normal_), TRUE);
    SendMessageW(hwnd_btn_test_alert_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_normal_), TRUE);
    SendMessageW(hwnd_btn_exit_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_normal_), TRUE);
    SendMessageW(hwnd_footer_, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_normal_), TRUE);

    // 6. Add taskbar notification area icon
    ZeroMemory(&nid_, sizeof(NOTIFYICONDATAW));
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwnd_;
    nid_.uID = TRAY_ICON_UID;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = icon_;
    wcsncpy_s(nid_.szTip, L"BadBT Keystroke Injection Shield (Active)", _TRUNCATE);

    bool icon_added = false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (Shell_NotifyIconW(NIM_ADD, &nid_)) {
            icon_added = true;
            break;
        }
        Sleep(200);
    }

    if (!icon_added) {
        DWORD err = GetLastError();
        std::cerr << "[TrayApp] Error: Shell_NotifyIconW(NIM_ADD) failed. GetLastError=" << err
                  << ", hwnd=" << hwnd_ << ", icon=" << icon_ << "\n";
        return false;
    }

    // 7. Initialize LiveDetector with canonical settings
    DetectorConfig d_cfg;
    d_cfg.window_size = 25;
    d_cfg.window_step = 1;
    d_cfg.threshold = config_.threshold;
    d_cfg.cooldown_seconds = config_.cooldown_seconds;
    d_cfg.model_path = find_model_path(config_.model_path);

    detector_ = std::make_unique<LiveDetector>(d_cfg);
    if (!detector_->initialize()) {
        std::cerr << "[TrayApp] Warning: Failed to load model from " << d_cfg.model_path << "\n";
    }

    // 8. Setup detector alert callback
    detector_->set_detection_callback([this](const DetectionResult& res) {
        total_windows_++;
        if (res.alert_triggered) {
            is_blocking_active_.store(true, std::memory_order_release);
            total_alerts_++;
            if (hwnd_) {
                PostMessageW(hwnd_, WM_DETECTOR_ALERT, 0, 0);
            }
        }
    });

    // 9. Associate instance and install WH_KEYBOARD_LL hook on UI thread
    s_instance_.store(this, std::memory_order_release);
    install_keyboard_hook();

    // 10. Synchronize initial UI state
    update_ui_state();

    // 10. Start background capture / replay thread
    if (config_.enable_capture_thread) {
        capture_thread_running_ = true;
        capture_thread_ = std::thread(&TrayApp::run_capture_loop, this);
    }

    // 11. Timers for tests/verification if requested
    if (config_.test_notification_on_start) {
        SetTimer(hwnd_, TEST_NOTIF_TIMER_ID, 1000, nullptr);
    }
    if (config_.auto_exit_seconds > 0) {
        SetTimer(hwnd_, AUTO_EXIT_TIMER_ID, static_cast<UINT>(config_.auto_exit_seconds) * 1000, nullptr);
    }

    // 12. Show desktop window unless configured to start minimized
    if (!config_.start_minimized) {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
    }

    return true;
}

int TrayApp::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void TrayApp::shutdown() {
    if (!is_running_.exchange(false)) {
        return; // Already shut down
    }

    // 1. Disable keyboard blocking immediately
    is_blocking_active_.store(false, std::memory_order_release);

    // 2. Uninstall keyboard hook before tearing down app state
    uninstall_keyboard_hook();

    // 3. Clear static instance pointer
    s_instance_.store(nullptr, std::memory_order_release);

    // Stop capture thread
    if (capture_thread_running_.load()) {
        capture_thread_running_ = false;
        if (hwnd_capture_) {
            PostMessageW(hwnd_capture_, WM_CLOSE, 0, 0);
        }
    }
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }

    // Remove tray icon
    if (hwnd_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    // Free typography resources
    if (hfont_title_)  { DeleteObject(hfont_title_);  hfont_title_ = nullptr; }
    if (hfont_bold_)   { DeleteObject(hfont_bold_);   hfont_bold_ = nullptr; }
    if (hfont_normal_) { DeleteObject(hfont_normal_); hfont_normal_ = nullptr; }
    if (hfont_status_) { DeleteObject(hfont_status_); hfont_status_ = nullptr; }
}

void TrayApp::show_main_window() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
}

std::wstring TrayApp::current_status_string() const {
    if (!detection_enabled_.load()) {
        return L"Disabled";
    }
    if (detector_ && detector_->state() == DetectorState::Cooldown) {
        return L"Cooldown";
    }
    return L"Monitoring";
}

void TrayApp::update_ui_state() {
    if (!hwnd_) return;

    if (detector_ && detector_->state() == DetectorState::Monitoring && is_blocking_active_.load(std::memory_order_acquire)) {
        is_blocking_active_.store(false, std::memory_order_release);
    }

    std::wstring status_str = current_status_string();
    std::wstring detail_str;
    std::wstring toggle_str;

    if (status_str == L"Disabled") {
        detail_str = L"Protection is paused. Keyboard input is not monitored.";
        toggle_str = L"Enable Protection";
    } else if (status_str == L"Cooldown") {
        detail_str = L"Injection detected! 30-second alert cooldown active. Resets upon expiry.";
        toggle_str = L"Disable Protection";
    } else {
        detail_str = L"Active - Monitoring Bluetooth keystrokes in real-time.";
        toggle_str = L"Disable Protection";
    }

    if (hwnd_status_val_) {
        SetWindowTextW(hwnd_status_val_, status_str.c_str());
        InvalidateRect(hwnd_status_val_, nullptr, TRUE);
    }
    if (hwnd_state_detail_) {
        SetWindowTextW(hwnd_state_detail_, detail_str.c_str());
    }
    if (hwnd_last_detect_val_) {
        SetWindowTextW(hwnd_last_detect_val_, last_alert_time_str_.c_str());
    }
    if (hwnd_btn_toggle_) {
        SetWindowTextW(hwnd_btn_toggle_, toggle_str.c_str());
    }

    update_tray_tooltip();
}

void TrayApp::toggle_detection() {
    bool current = detection_enabled_.load();
    bool next = !current;
    detection_enabled_.store(next);

    if (hwnd_) {
        KillTimer(hwnd_, COOLDOWN_TIMER_ID);
    }
    if (detector_) {
        detector_->reset_detection_state();
    }
    reset_capture_state_.store(true);
    is_blocking_active_.store(false, std::memory_order_release);
    update_ui_state();

    if (next) {
        show_notification(
            L"BadBT Shield Active",
            L"Keystroke injection monitoring is now ACTIVE.",
            NIIF_INFO
        );
    } else {
        show_notification(
            L"BadBT Shield Paused",
            L"Keystroke injection monitoring has been PAUSED.",
            NIIF_WARNING
        );
    }
}

void TrayApp::update_tray_tooltip() {
    if (!hwnd_) return;
    std::wstring status = current_status_string();
    if (status == L"Disabled") {
        wcsncpy_s(nid_.szTip, L"BadBT Keystroke Injection Shield (Stopped)", _TRUNCATE);
    } else if (status == L"Cooldown") {
        wcsncpy_s(nid_.szTip, L"BadBT Keystroke Injection Shield (Cooldown)", _TRUNCATE);
    } else {
        wcsncpy_s(nid_.szTip, L"BadBT Keystroke Injection Shield (Active)", _TRUNCATE);
    }
    nid_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayApp::show_notification(const std::wstring& title, const std::wstring& message, DWORD flags) {
    show_tray_notification(hwnd_, TRAY_ICON_UID, title, message, flags);
}

void TrayApp::on_detector_alert() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_s(&tm_buf, &in_time_t);
    wchar_t time_buf[64];
    wcsftime(time_buf, sizeof(time_buf) / sizeof(wchar_t), L"%H:%M:%S (%Y-%m-%d)", &tm_buf);
    last_alert_time_str_ = time_buf;

    show_notification(
        L"BadBT: Keystroke Injection Detected!",
        L"Suspicious rapid automated keystrokes detected on Bluetooth keyboard (P >= 0.95).",
        NIIF_WARNING | NIIF_LARGE_ICON
    );

    // Arm 30-second cooldown timer on UI thread to reset back to Monitoring when cooldown expires
    if (hwnd_) {
        SetTimer(hwnd_, COOLDOWN_TIMER_ID, static_cast<UINT>(config_.cooldown_seconds * 1000), nullptr);
        update_ui_state();
    }
}

void TrayApp::handle_tray_icon(LPARAM lParam) {
    switch (lParam) {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        show_main_window();
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        show_context_menu();
        break;
    }
}

void TrayApp::show_context_menu() {
    if (!hwnd_) return;

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // 1. Open BadBT Window
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN, L"Open BadBT Window");
    SetMenuDefaultItem(hMenu, IDM_OPEN, FALSE);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 2. Status display (grayed/disabled item)
    std::wstring status_str;
    std::wstring cur = current_status_string();
    if (cur == L"Disabled") {
        status_str = L"Status: Stopped (Paused)";
    } else if (cur == L"Cooldown") {
        status_str = L"Status: Cooldown (Shield Active)";
    } else {
        status_str = L"Status: Running (Monitoring)";
    }
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED | MF_DISABLED, IDM_STATUS, status_str.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 3. Enable / Disable toggle
    std::wstring toggle_str = detection_enabled_.load()
        ? L"Disable Detection"
        : L"Enable Detection";
    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE, toggle_str.c_str());

    // 4. Test Alert notification trigger
    AppendMenuW(hMenu, MF_STRING, IDM_TEST_NOTIFICATION, L"Trigger Test Alert");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 5. Exit
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);

    SetForegroundWindow(hwnd_);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

void TrayApp::run_capture_loop() {
    if (!config_.replay_file.empty()) {
        std::cout << "[TrayApp] Replaying test file: " << config_.replay_file << "\n";
        detector_->replay_jsonl(config_.replay_file);
        // Keep capture thread alive while application runs
        while (capture_thread_running_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return;
    }

    // ── Live Raw Input capture mode ──
    std::unordered_map<HANDLE, std::wstring> device_cache;
    std::unordered_map<HANDLE, bool> bt_status_cache;
    std::unordered_map<uint64_t, bool> key_down_state;

    auto key_state_id = [](HANDLE device, USHORT scan_code) -> uint64_t {
        return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(device)) << 16)
               ^ static_cast<uint64_t>(scan_code);
    };

    KeyEventCallback callback = [this, &device_cache, &bt_status_cache, &key_down_state, key_state_id](const RawKeyEvent& raw_event) {
        if (!detection_enabled_.load()) {
            return;
        }

        if (reset_capture_state_.exchange(false)) {
            key_down_state.clear();
        }

        // Cache device path and Bluetooth status
        auto it = device_cache.find(raw_event.device);
        if (it == device_cache.end()) {
            std::wstring path = get_device_path(raw_event.device);
            device_cache[raw_event.device] = path;
            bt_status_cache[raw_event.device] = is_bluetooth_device(path);
            it = device_cache.find(raw_event.device);
        }

        // Bluetooth filter
        if (config_.filter_bluetooth && !bt_status_cache[raw_event.device]) {
            return;
        }

        // Autorepeat tracking
        uint64_t state_id = key_state_id(raw_event.device, raw_event.scan_code);
        bool is_keyup = (raw_event.flags & 0x01) != 0;
        bool is_autorepeat = false;
        if (is_keyup) {
            key_down_state[state_id] = false;
        } else {
            is_autorepeat = key_down_state[state_id];
            key_down_state[state_id] = true;
        }

        InputEvent ev;
        ev.timestamp_ns = raw_event.timestamp_ns;
        ev.vkey = raw_event.vkey;
        ev.scan_code = raw_event.scan_code;
        ev.flags = raw_event.flags;
        ev.is_autorepeat = is_autorepeat;
        ev.device_path = wstring_to_utf8(it->second);

        detector_->process_event(ev);
    };

    hwnd_capture_ = create_capture_window(&callback);
    if (!hwnd_capture_) {
        std::cerr << "[TrayApp] Error: Failed to create raw input capture window.\n";
        return;
    }

    run_message_loop(hwnd_capture_, capture_thread_running_, 0);

    destroy_capture_window(hwnd_capture_);
    hwnd_capture_ = nullptr;
}

LRESULT CALLBACK TrayApp::wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!app) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_TRAYICON:
        app->handle_tray_icon(lParam);
        return 0;

    case WM_DETECTOR_ALERT:
        app->on_detector_alert();
        return 0;

    case WM_TEST_NOTIFICATION:
        app->show_notification(
            L"BadBT Shield: Test Notification",
            L"The BadBT keystroke injection detector is running and operational.",
            NIIF_INFO | NIIF_LARGE_ICON
        );
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = reinterpret_cast<HDC>(wParam);
        HWND hwndCtl = reinterpret_cast<HWND>(lParam);
        SetBkMode(hdcStatic, TRANSPARENT);

        if (hwndCtl == app->hwnd_status_val_) {
            std::wstring status = app->current_status_string();
            if (status == L"Disabled") {
                SetTextColor(hdcStatic, RGB(110, 110, 110)); // Gray
            } else if (status == L"Cooldown") {
                SetTextColor(hdcStatic, RGB(220, 20, 20));   // Red
            } else {
                SetTextColor(hdcStatic, RGB(0, 140, 20));    // Green
            }
        } else if (hwndCtl == app->hwnd_title_) {
            SetTextColor(hdcStatic, RGB(20, 20, 20));
        } else if (hwndCtl == app->hwnd_subtitle_) {
            SetTextColor(hdcStatic, RGB(100, 100, 100));
        } else {
            SetTextColor(hdcStatic, RGB(40, 40, 40));
        }
        return reinterpret_cast<INT_PTR>(app->hbr_bg_);
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_OPEN:
            app->show_main_window();
            return 0;
        case IDM_TOGGLE:
        case IDC_BTN_TOGGLE:
            app->toggle_detection();
            return 0;
        case IDM_TEST_NOTIFICATION:
        case IDC_BTN_TEST_ALERT:
            PostMessageW(hwnd, WM_TEST_NOTIFICATION, 0, 0);
            return 0;
        case IDM_EXIT:
        case IDC_BTN_EXIT:
            app->shutdown();
            PostQuitMessage(0);
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == TEST_NOTIF_TIMER_ID) {
            KillTimer(hwnd, TEST_NOTIF_TIMER_ID);
            PostMessageW(hwnd, WM_TEST_NOTIFICATION, 0, 0);
            return 0;
        } else if (wParam == AUTO_EXIT_TIMER_ID) {
            KillTimer(hwnd, AUTO_EXIT_TIMER_ID);
            app->shutdown();
            PostQuitMessage(0);
            return 0;
        } else if (wParam == COOLDOWN_TIMER_ID) {
            KillTimer(hwnd, COOLDOWN_TIMER_ID);
            if (app->detector()) {
                app->detector()->reset_detection_state();
            }
            app->reset_capture_state();
            app->set_blocking_active(false);
            app->update_ui_state();
            return 0;
        }
        break;

    case WM_CLOSE:
        // Hide to system tray instead of exiting immediately
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        app->shutdown();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace badbt

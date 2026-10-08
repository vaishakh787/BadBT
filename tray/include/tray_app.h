#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include "../../detector/include/detector.h"

namespace badbt {

// Custom Window Messages
constexpr UINT WM_TRAYICON           = WM_APP + 1;
constexpr UINT WM_DETECTOR_ALERT     = WM_APP + 2;
constexpr UINT WM_TEST_NOTIFICATION  = WM_APP + 3;

// Menu & Control IDs
constexpr UINT IDM_OPEN              = 1000;
constexpr UINT IDM_STATUS            = 1001;
constexpr UINT IDM_TOGGLE            = 1002;
constexpr UINT IDM_TEST_NOTIFICATION = 1003;
constexpr UINT IDM_EXIT              = 1004;

constexpr UINT IDC_BTN_TOGGLE        = 2001;
constexpr UINT IDC_BTN_TEST_ALERT    = 2002;
constexpr UINT IDC_BTN_EXIT          = 2003;

constexpr UINT IDC_STATUS_VALUE      = 2010;
constexpr UINT IDC_STATE_DETAIL      = 2011;
constexpr UINT IDC_LAST_DETECT_VALUE = 2012;

constexpr UINT TRAY_ICON_UID         = 1;
constexpr UINT_PTR AUTO_EXIT_TIMER_ID= 101;
constexpr UINT_PTR TEST_NOTIF_TIMER_ID= 102;
constexpr UINT_PTR COOLDOWN_TIMER_ID = 103;

struct TrayConfig {
    bool        filter_bluetooth = true;
    std::string model_path = "";
    std::string replay_file = "";
    bool        test_notification_on_start = false;
    int         auto_exit_seconds = 0; // 0 = run indefinitely
    float       threshold = 0.95f;
    double      cooldown_seconds = 30.0;
    bool        start_minimized = false;
    bool        enable_capture_thread = true; // Set false for deterministic unit tests
    bool        enable_keyboard_hook = true;  // Set false to skip SetWindowsHookExW in tests if desired
};

class TrayApp {
public:
    explicit TrayApp(const TrayConfig& config = TrayConfig());
    ~TrayApp();

    TrayApp(const TrayApp&) = delete;
    TrayApp& operator=(const TrayApp&) = delete;

    // Initialize UI window, tray icon, detector, and background capture
    bool initialize();

    // Run the UI message loop (blocks until exit)
    int run();

    // Programmatically request shutdown
    void shutdown();

    // Toggle detection on/off
    void toggle_detection();

    // Show or restore main window
    void show_main_window();

    // Synchronize UI labels and colors with current detector state
    void update_ui_state();

    // Accessors
    bool is_detection_running() const { return detection_enabled_.load(std::memory_order_acquire); }
    bool is_blocking_active() const { return is_blocking_active_.load(std::memory_order_acquire); }
    void set_blocking_active(bool active) { is_blocking_active_.store(active, std::memory_order_release); }
    HHOOK keyboard_hook() const { return hook_; }
    size_t total_alerts() const { return total_alerts_.load(); }
    size_t total_windows() const { return total_windows_.load(); }
    HWND hwnd() const { return hwnd_; }
    LiveDetector* detector() const { return detector_.get(); }
    void reset_capture_state() { reset_capture_state_.store(true); }
    std::wstring last_alert_time_string() const { return last_alert_time_str_; }
    std::wstring current_status_string() const;

    // Low-level keyboard hook procedure (Win32 callback)
    static LRESULT CALLBACK low_level_keyboard_proc(int nCode, WPARAM wParam, LPARAM lParam);

private:
    static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void handle_tray_icon(LPARAM lParam);
    void show_context_menu();
    void update_tray_tooltip();
    void run_capture_loop();
    void on_detector_alert();
    void show_notification(const std::wstring& title, const std::wstring& message, DWORD flags = NIIF_WARNING | NIIF_LARGE_ICON);

    TrayConfig config_;
    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_{};
    HICON icon_ = nullptr;

    // Child UI controls
    HWND hwnd_title_ = nullptr;
    HWND hwnd_subtitle_ = nullptr;
    HWND hwnd_group_ = nullptr;
    HWND hwnd_status_lbl_ = nullptr;
    HWND hwnd_status_val_ = nullptr;
    HWND hwnd_state_lbl_ = nullptr;
    HWND hwnd_state_detail_ = nullptr;
    HWND hwnd_last_detect_lbl_ = nullptr;
    HWND hwnd_last_detect_val_ = nullptr;
    HWND hwnd_policy_lbl_ = nullptr;
    HWND hwnd_policy_val_ = nullptr;
    HWND hwnd_btn_toggle_ = nullptr;
    HWND hwnd_btn_test_alert_ = nullptr;
    HWND hwnd_btn_exit_ = nullptr;
    HWND hwnd_footer_ = nullptr;

    // GDI resources
    HFONT hfont_title_ = nullptr;
    HFONT hfont_bold_ = nullptr;
    HFONT hfont_normal_ = nullptr;
    HFONT hfont_status_ = nullptr;
    HBRUSH hbr_bg_ = nullptr;

    void install_keyboard_hook();
    void uninstall_keyboard_hook();

    // State display
    std::wstring last_alert_time_str_ = L"None";

    static std::atomic<TrayApp*> s_instance_;

    std::atomic<bool> is_running_{true};
    std::atomic<bool> detection_enabled_{true};
    std::atomic<bool> is_blocking_active_{false};
    HHOOK hook_ = nullptr;
    std::atomic<bool> capture_thread_running_{false};
    std::atomic<bool> reset_capture_state_{false};
    std::atomic<size_t> total_alerts_{0};
    std::atomic<size_t> total_windows_{0};

    std::unique_ptr<LiveDetector> detector_;
    std::thread capture_thread_;
    HWND hwnd_capture_ = nullptr;
};

} // namespace badbt

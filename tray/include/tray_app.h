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
#include <functional>
#include <mutex>
#include <set>
#include <map>
#include <chrono>
#include <vector>
#include "../../detector/include/detector.h"
#include "blocklist.h"
#include "bt_disconnect.h"

namespace badbt {

// Custom Window Messages
constexpr UINT WM_TRAYICON           = WM_APP + 1;
constexpr UINT WM_DETECTOR_ALERT     = WM_APP + 2;
constexpr UINT WM_TEST_NOTIFICATION  = WM_APP + 3;
constexpr UINT WM_DISCONNECT_DONE    = WM_APP + 4; // lParam: heap-allocated DisconnectOutcome*
constexpr UINT WM_DISCONNECT_STARTED = WM_APP + 5; // arms the input-gate watchdog timer

// Menu & Control IDs
constexpr UINT IDM_OPEN              = 1000;
constexpr UINT IDM_STATUS            = 1001;
constexpr UINT IDM_TOGGLE            = 1002;
constexpr UINT IDM_TEST_NOTIFICATION = 1003;
constexpr UINT IDM_EXIT              = 1004;
constexpr UINT IDM_RESET_BLOCKLIST   = 1005;

constexpr UINT IDC_BTN_TOGGLE        = 2001;
constexpr UINT IDC_BTN_TEST_ALERT    = 2002;
constexpr UINT IDC_BTN_EXIT          = 2003;
constexpr UINT IDC_BTN_RESET         = 2004;

constexpr UINT IDC_STATUS_VALUE      = 2010;
constexpr UINT IDC_STATE_DETAIL      = 2011;
constexpr UINT IDC_LAST_DETECT_VALUE = 2012;
constexpr UINT IDC_BLOCKED_VALUE     = 2013;

constexpr UINT TRAY_ICON_UID         = 1;
constexpr UINT_PTR AUTO_EXIT_TIMER_ID= 101;
constexpr UINT_PTR TEST_NOTIF_TIMER_ID= 102;
constexpr UINT_PTR COOLDOWN_TIMER_ID = 103;
constexpr UINT_PTR DISCONNECT_GATE_TIMER_ID = 104;

struct TrayConfig {
    bool        filter_bluetooth = true;
    std::string model_path = "";
    std::string replay_file = "";
    bool        test_notification_on_start = false;
    int         auto_exit_seconds = 0; // 0 = run indefinitely
    size_t      window_size = 25;
    float       threshold = 0.95f;
    double      cooldown_seconds = 30.0;
    bool        start_minimized = false;
    bool        enable_capture_thread = true; // Set false for deterministic unit tests
    bool        enable_keyboard_hook = true;  // Set false to skip SetWindowsHookExW in tests if desired
    std::string blocklist_path = "";          // Override persisted blocklist path (default: %APPDATA%\BadBT\blocklist.txt)

    // On detection, disconnect (and unpair) the offending Bluetooth device instead of
    // leaving the whole keyboard blocked. Input is blocked only briefly while the disconnect
    // runs (at most disconnect_gate_ms). Set false for the legacy block-until-Reset behavior.
    bool        disconnect_on_detect = true;
    int         disconnect_gate_ms = 4000;   // hard cap on the temporary input block
    // If a disconnect fails, keep the whole keyboard blocked until Reset (old behavior).
    // Default false: a failed disconnect must not lock the user out; it is retried instead.
    bool        keep_block_on_disconnect_failure = false;
};

// Result of one background disconnect attempt, delivered to the UI thread.
struct DisconnectOutcome {
    std::string      device_path;
    DisconnectResult result;
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

    // Clear the device blocklist, lift keyboard blocking, and return to Monitoring.
    // This is the user-facing recovery action (tray "Reset Blocklist" / window "Reset").
    void reset_blocklist();

    // Replace the function used to disconnect a device (default: disconnect_bluetooth_device),
    // and switch disconnect-on-detect on/off at runtime. Test hooks: call from the UI thread
    // while no disconnect is in flight.
    using DisconnectFn = std::function<DisconnectResult(const std::string&)>;
    void set_disconnect_fn(DisconnectFn fn) { disconnect_fn_ = std::move(fn); }
    void set_disconnect_on_detect(bool enabled) { config_.disconnect_on_detect = enabled; }
    void set_disconnect_gate_ms(int ms) { config_.disconnect_gate_ms = ms; }

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
    size_t total_disconnects() const { return total_disconnects_.load(); }
    size_t blocklist_size() const { return blocklist_.size(); }
    const Blocklist& blocklist() const { return blocklist_; }
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

    // Disconnect a blocklisted device on a worker thread (deduplicated per device). Engages
    // the temporary input block until the attempt finishes; the result comes back to the UI
    // thread as WM_DISCONNECT_DONE.
    void request_disconnect(const std::string& device_path);
    void on_disconnect_done(const DisconnectOutcome& outcome);
    void on_disconnect_gate_timeout();
    void engage_gate();
    bool standing_block_required_locked() const; // caller holds disconnect_mutex_
    bool standing_block_required() const;
    void join_disconnect_threads();
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
    HWND hwnd_blocked_lbl_ = nullptr;
    HWND hwnd_blocked_val_ = nullptr;
    HWND hwnd_policy_lbl_ = nullptr;
    HWND hwnd_policy_val_ = nullptr;
    HWND hwnd_btn_toggle_ = nullptr;
    HWND hwnd_btn_test_alert_ = nullptr;
    HWND hwnd_btn_reset_ = nullptr;
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
    std::atomic<size_t> total_disconnects_{0};

    // Disconnect-on-detect bookkeeping.
    DisconnectFn disconnect_fn_ = [](const std::string& p) { return disconnect_bluetooth_device(p); };
    struct DisconnectWorker {
        std::thread                         thread;
        std::shared_ptr<std::atomic<bool>>  done;
    };
    mutable std::mutex        disconnect_mutex_;     // guards the next two members
    // device path -> when its disconnect started; an entry older than 10s is treated as hung
    std::map<std::string, std::chrono::steady_clock::time_point> disconnect_inflight_;
    std::vector<DisconnectWorker> disconnect_workers_;
    std::set<std::string>     disconnect_failed_;    // UI thread only: devices we could not disconnect
    std::atomic<bool>         needs_manual_reset_{false}; // an alert we could not act on per-device

    std::unique_ptr<LiveDetector> detector_;
    std::thread capture_thread_;
    HWND hwnd_capture_ = nullptr;

    // Persistent device blocklist + its on-disk location.
    Blocklist   blocklist_;
    std::string blocklist_path_;

    // Refresh the "Blocked devices: N" line and tray tooltip from the current blocklist.
    void update_blocked_display();
};

} // namespace badbt

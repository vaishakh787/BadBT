#include "raw_input.h"
#include <chrono>
#include <vector>

namespace badbt {

// ─── Constants ──────────────────────────────────────────────────────

static const wchar_t* CAPTURE_WINDOW_CLASS = L"BadBTCaptureWindowClass";
static const UINT_PTR DURATION_TIMER_ID = 1;

// ─── Window Procedure ───────────────────────────────────────────────

static LRESULT CALLBACK capture_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_INPUT: {
        // Timestamp as early as possible for accurate timing data
        auto now = std::chrono::system_clock::now();
        int64_t ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();

        // Monotonic high-res counter — immune to system clock adjustments,
        // used as the primary source for dwell/flight time deltas.
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);

        // Get raw input data size
        UINT size = 0;
        GetRawInputData(
            reinterpret_cast<HRAWINPUT>(lParam),
            RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)
        );
        if (size == 0) break;

        // Read raw input data
        std::vector<BYTE> buffer(size);
        UINT read = GetRawInputData(
            reinterpret_cast<HRAWINPUT>(lParam),
            RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)
        );
        if (read != size) break;

        auto* raw = reinterpret_cast<RAWINPUT*>(buffer.data());

        // Only process keyboard events
        if (raw->header.dwType == RIM_TYPEKEYBOARD) {
            auto* callback = reinterpret_cast<KeyEventCallback*>(
                GetWindowLongPtr(hwnd, GWLP_USERDATA)
            );
            if (callback) {
                RawKeyEvent event{};
                event.timestamp_ns    = ts_ns;
                event.qpc_ticks       = qpc.QuadPart;
                event.device          = raw->header.hDevice;
                event.vkey            = raw->data.keyboard.VKey;
                event.scan_code       = raw->data.keyboard.MakeCode;
                event.flags           = raw->data.keyboard.Flags;
                event.message         = raw->data.keyboard.Message;
                (*callback)(event);
            }
        }

        // Must call DefWindowProc for WM_INPUT to free the internal buffer
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    case WM_TIMER:
        if (wParam == DURATION_TIMER_ID) {
            // Duration expired — break the message loop
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ─── Public API ─────────────────────────────────────────────────────

HWND create_capture_window(KeyEventCallback* callback) {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    // Register window class (idempotent — will fail silently if already registered)
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = capture_wndproc;
    wc.hInstance      = hInstance;
    wc.lpszClassName  = CAPTURE_WINDOW_CLASS;
    RegisterClassExW(&wc);

    // Create a message-only window (no visible UI)
    HWND hwnd = CreateWindowExW(
        0, CAPTURE_WINDOW_CLASS, L"BadBT Capture",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,   // Message-only window
        nullptr, hInstance, nullptr
    );
    if (!hwnd) return nullptr;

    // Store callback pointer via window user data
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(callback));

    // Register for raw keyboard input with INPUTSINK
    // (receives input even when the window is not in the foreground)
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;    // HID_USAGE_PAGE_GENERIC
    rid.usUsage     = 0x06;    // HID_USAGE_GENERIC_KEYBOARD
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = hwnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        DestroyWindow(hwnd);
        return nullptr;
    }

    return hwnd;
}

void run_message_loop(HWND hwnd, std::atomic<bool>& running, int duration_secs) {
    // Set a timer if duration is specified
    if (duration_secs > 0) {
        SetTimer(hwnd, DURATION_TIMER_ID, static_cast<UINT>(duration_secs) * 1000, nullptr);
    }

    MSG msg;
    while (running.load(std::memory_order_relaxed)) {
        // Use PeekMessage with a short sleep to allow checking `running` flag
        // (GetMessage blocks indefinitely, can't check atomic)
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            // No messages pending — sleep briefly to avoid busy-waiting
            // 1ms is fine for keystroke capture (human typing is 50-200ms between keys)
            Sleep(1);
        }
    }

    // Clean up timer
    if (duration_secs > 0) {
        KillTimer(hwnd, DURATION_TIMER_ID);
    }
}

void destroy_capture_window(HWND hwnd) {
    if (hwnd) {
        DestroyWindow(hwnd);
    }
    UnregisterClassW(CAPTURE_WINDOW_CLASS, GetModuleHandle(nullptr));
}

} // namespace badbt

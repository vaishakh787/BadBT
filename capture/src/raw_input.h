#pragma once

// BadBT Detection — Raw Input Capture
// Registers for keyboard Raw Input and runs a Win32 message loop to capture events.
// Uses a hidden window with RIDEV_INPUTSINK to receive input even when not focused.

#include <functional>
#include <atomic>
#include <cstdint>
#include <Windows.h>

namespace badbt {

// Raw keyboard event as delivered by WM_INPUT
struct RawKeyEvent {
    int64_t timestamp_ns;    // System clock, nanoseconds since epoch
    int64_t qpc_ticks;       // QueryPerformanceCounter value at capture time
    HANDLE  device;          // Raw Input device handle (pass to get_device_path)
    USHORT  vkey;            // Virtual key code
    USHORT  scan_code;       // Hardware scan code
    USHORT  flags;           // RI_KEY_MAKE (0) = key down, RI_KEY_BREAK (1) = key up
    UINT    message;         // raw->data.keyboard.Message (WM_KEYDOWN, WM_SYSKEYDOWN, etc.)
};

// Callback invoked for each raw keyboard event
using KeyEventCallback = std::function<void(const RawKeyEvent&)>;

// Create a hidden window and register for raw keyboard input.
// Returns the window handle, or NULL on failure.
// The callback is stored via GWLP_USERDATA and invoked from the WndProc.
HWND create_capture_window(KeyEventCallback* callback);

// Run the Win32 message loop until `running` becomes false or duration expires.
// If duration_secs <= 0, runs until `running` is set to false (e.g. via Ctrl+C).
// Call PostMessage(hwnd, WM_CLOSE, 0, 0) from another thread to break the loop.
void run_message_loop(HWND hwnd, std::atomic<bool>& running, int duration_secs = 0);

// Clean up: destroy window, unregister class.
void destroy_capture_window(HWND hwnd);

} // namespace badbt

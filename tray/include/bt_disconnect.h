#pragma once

#include <cstdint>
#include <string>

namespace badbt {

struct DisconnectResult {
    bool        success = false; // true if the device was disconnected/removed/disabled
    std::string detail;          // human-readable outcome (for logs and notifications)
    std::string mac_key;         // "bt:AABBCCDDEEFF" if the device's MAC was found, else empty
};

// Resolve the Bluetooth MAC of the device behind a Raw Input device path (Windows only).
// Returns false for non-Bluetooth devices or if the devnode cannot be found.
bool resolve_bt_address(const std::string& raw_input_path, uint64_t& address);

// Disconnect the Bluetooth device behind a Raw Input device path (the same string the
// detector reports as primary_device_path / that the Blocklist stores).
//
// Windows only (implemented in bt_disconnect.cpp). Steps:
//   1. Raw Input path -> PnP devnode, then walk up the parents to find the device's MAC.
//   2. Classic Bluetooth: on each radio that knows the MAC, disable the HID service, and
//      call BluetoothRemoveDevice with the MAC (works even if no radio "knows" the device).
//   3. Bluetooth LE (e.g. a Flipper Zero): unpair through WinRT, since the Win32 APIs do not
//      see LE-only devices.
//   4. Last resort: disable the HID devnode, which needs BadBT to run elevated.
// Every step is written to %APPDATA%\BadBT\badbt.log.
//
// Blocking: these are synchronous Bluetooth API calls that can take a second or more, so
// call from a worker thread, never from the keyboard hook or the UI thread.
DisconnectResult disconnect_bluetooth_device(const std::string& raw_input_path);

} // namespace badbt

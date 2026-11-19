#pragma once

#include <string>

namespace badbt {

struct DisconnectResult {
    bool        success = false; // true if the device was disconnected/removed/disabled
    std::string detail;          // human-readable outcome (for logs and notifications)
};

// Disconnect the Bluetooth device behind a Raw Input device path (the same string the
// detector reports as primary_device_path / that the Blocklist stores).
//
// Windows only (implemented in bt_disconnect.cpp). Steps:
//   1. Raw Input path -> PnP devnode, then walk up the parents to find the device's MAC.
//   2. On each Bluetooth radio that knows that MAC: disable its HID service (drops the
//      live keyboard connection) and remove (unpair) the device so it cannot silently
//      reconnect.
//   3. If neither worked (no address / API refused), try disabling the HID devnode,
//      which succeeds only when BadBT runs elevated.
//
// Blocking: these are synchronous Bluetooth API calls that can take a second or more, so
// call from a worker thread, never from the keyboard hook or the UI thread.
DisconnectResult disconnect_bluetooth_device(const std::string& raw_input_path);

} // namespace badbt

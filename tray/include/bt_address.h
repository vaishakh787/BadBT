#pragma once

#include <cstdint>
#include <string>

namespace badbt {

// Pure string helpers (no Win32) for turning a Raw Input device path into the Bluetooth
// MAC address of the device behind it. Kept portable so they can be unit-tested anywhere.

// Convert a Raw Input device path, e.g.
//   \\?\HID#{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&09cc#9&2b1d3c2&0&0000#{4d1e55b2-...}
// into a PnP device instance ID:
//   HID\{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&09cc\9&2b1d3c2&0&0000
// Returns an empty string if the path is empty.
std::string raw_input_path_to_instance_id(const std::string& raw_path);

// Find a 48-bit Bluetooth address embedded in a PnP instance ID. Recognizes the forms used
// by Windows Bluetooth enumerators, e.g.
//   BTHENUM\{...}_VID&..._PID&...\9&2b1d3c2&0&AABBCCDDEEFF_C00000000   (classic HID)
//   BTHLE\DEV_AABBCCDDEEFF\7&1a2b3c4d&0&AABBCCDDEEFF                   (LE device)
//   BTHLEDEVICE\{...}_DEV_VID&..._PID&..._REV&..._AABBCCDDEEFF\8&...   (LE HID service)
// Returns true and sets `address` (big-endian value of the MAC) on success. An all-zero
// address is rejected.
bool parse_bt_address(const std::string& instance_id, uint64_t& address);

// "AA:BB:CC:DD:EE:FF" form for logs/notifications.
std::string format_bt_address(uint64_t address);

} // namespace badbt

#pragma once

// BadBT Detection — Device Info
// Queries Win32 Raw Input device paths and detects Bluetooth devices.

#include <string>
#include <Windows.h>

namespace badbt {

// Get the device path string for a Raw Input device handle.
// Returns empty string on failure.
std::wstring get_device_path(HANDLE device);

// Check if a device path indicates a Bluetooth-connected device.
// Looks for "BTHENUM" (Classic) or "BTHLEDevice" (BLE) in the path.
bool is_bluetooth_device(const std::wstring& device_path);

// Convert a wide string (UTF-16) to a UTF-8 std::string.
std::string wstring_to_utf8(const std::wstring& wstr);

} // namespace badbt

#pragma once

#include <cstdint>
#include <string>

namespace badbt {

// Unpair a Bluetooth LE device through WinRT (Windows.Devices.Bluetooth). The classic
// Win32 Bluetooth APIs generally do not know LE-only devices (e.g. a Flipper Zero acting
// as a BLE keyboard), so this is the path that actually drops them without admin rights.
// Blocks for up to a few seconds: call from a worker thread. `detail` receives a short
// description of the outcome. Returns true if the device is now unpaired.
bool winrt_unpair_le_device(uint64_t address, std::string& detail);

} // namespace badbt

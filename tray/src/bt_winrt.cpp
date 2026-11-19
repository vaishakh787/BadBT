#include "bt_winrt.h"

#include <windows.h>
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>

#include <chrono>
#include <cstdio>

namespace badbt {

namespace {

using winrt::Windows::Foundation::AsyncStatus;

const char* unpair_status_name(winrt::Windows::Devices::Enumeration::DeviceUnpairingResultStatus s) {
    using S = winrt::Windows::Devices::Enumeration::DeviceUnpairingResultStatus;
    switch (s) {
    case S::Unpaired:                   return "Unpaired";
    case S::AlreadyUnpaired:            return "AlreadyUnpaired";
    case S::OperationAlreadyInProgress: return "OperationAlreadyInProgress";
    case S::AccessDenied:               return "AccessDenied";
    case S::Failed:                     return "Failed";
    default:                            return "Unknown";
    }
}

std::string hr_hex(int32_t hr) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(hr));
    return buf;
}

// Wait for an async operation with a deadline instead of blocking forever; cancel on timeout.
template <typename Op>
bool await_for(const Op& op, int timeout_ms) {
    if (op.wait_for(std::chrono::milliseconds(timeout_ms)) == AsyncStatus::Completed) {
        return true;
    }
    op.Cancel();
    return false;
}

} // namespace

bool winrt_unpair_le_device(uint64_t address, std::string& detail) {
    using namespace winrt::Windows::Devices::Bluetooth;
    using S = winrt::Windows::Devices::Enumeration::DeviceUnpairingResultStatus;

    try {
        // Worker threads start with no apartment; waiting on WinRT operations wants MTA.
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        // Already initialized on this thread with another model; carry on.
    }

    try {
        // A Flipper Zero uses a random static address, which the plain overload may not resolve,
        // so try the default first and then both explicit address types.
        BluetoothLEDevice device{nullptr};
        for (int attempt = 0; attempt < 3 && !device; ++attempt) {
            auto op = (attempt == 0)
                ? BluetoothLEDevice::FromBluetoothAddressAsync(address)
                : BluetoothLEDevice::FromBluetoothAddressAsync(
                      address, attempt == 1 ? BluetoothAddressType::Random : BluetoothAddressType::Public);
            if (await_for(op, 2500)) {
                device = op.GetResults();
            }
        }
        if (!device) {
            detail = "WinRT: LE device not found (or lookup timed out)";
            return false;
        }

        auto unpair = device.DeviceInformation().Pairing().UnpairAsync();
        if (!await_for(unpair, 3000)) {
            device.Close();
            detail = "WinRT LE unpair timed out";
            return false;
        }
        const S status = unpair.GetResults().Status();
        device.Close();
        detail = std::string("WinRT LE unpair: ") + unpair_status_name(status);
        return status == S::Unpaired || status == S::AlreadyUnpaired;
    } catch (const winrt::hresult_error& e) {
        detail = "WinRT LE unpair threw hr=" + hr_hex(static_cast<int32_t>(e.code()));
        return false;
    } catch (...) {
        detail = "WinRT LE unpair threw an unknown exception";
        return false;
    }
}

} // namespace badbt

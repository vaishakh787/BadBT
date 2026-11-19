#include "bt_winrt.h"

#include <windows.h>
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>

#include <cstdio>

namespace badbt {

namespace {

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

} // namespace

bool winrt_unpair_le_device(uint64_t address, std::string& detail) {
    using namespace winrt::Windows::Devices::Bluetooth;
    using S = winrt::Windows::Devices::Enumeration::DeviceUnpairingResultStatus;

    try {
        // Worker threads start with no apartment; blocking .get() needs MTA.
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        // Already initialized on this thread with another model; carry on.
    }

    try {
        BluetoothLEDevice device = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
        if (!device) {
            detail = "WinRT: LE device not known to Windows";
            return false;
        }
        auto result = device.DeviceInformation().Pairing().UnpairAsync().get();
        const S status = result.Status();
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

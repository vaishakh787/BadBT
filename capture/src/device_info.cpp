#include "device_info.h"
#include <algorithm>

namespace badbt {

std::wstring get_device_path(HANDLE device) {
    // First call: get required buffer size
    UINT size = 0;
    UINT result = GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &size);
    if (result == static_cast<UINT>(-1) || size == 0) {
        return L"";
    }

    // Second call: fill buffer
    std::wstring path(size, L'\0');
    result = GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, &path[0], &size);
    if (result == static_cast<UINT>(-1)) {
        return L"";
    }

    // Trim trailing nulls (GetRawInputDeviceInfo includes them in the count)
    while (!path.empty() && path.back() == L'\0') {
        path.pop_back();
    }

    return path;
}

bool is_bluetooth_device(const std::wstring& device_path) {
    // Case-insensitive search for Bluetooth device path markers
    // Classic Bluetooth: path contains "BTHENUM"
    // Bluetooth Low Energy: path contains "BTHLEDevice"
    std::wstring upper = device_path;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);

    return upper.find(L"BTHENUM") != std::wstring::npos ||
           upper.find(L"BTHLEDEVICE") != std::wstring::npos ||
           upper.find(L"{00001812-0000-1000-8000-00805F9B34FB}") != std::wstring::npos ||
           upper.find(L"_DEV_VID&02") != std::wstring::npos; // Bus type 02 = Bluetooth
}

std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";

    int size = WideCharToMultiByte(
        CP_UTF8, 0,
        wstr.c_str(), static_cast<int>(wstr.size()),
        nullptr, 0,
        nullptr, nullptr
    );
    if (size <= 0) return "";

    std::string result(size, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        wstr.c_str(), static_cast<int>(wstr.size()),
        &result[0], size,
        nullptr, nullptr
    );
    return result;
}

} // namespace badbt

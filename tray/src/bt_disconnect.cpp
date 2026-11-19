#include "bt_disconnect.h"
#include "bt_address.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <bluetoothapis.h>
#include <cfgmgr32.h>

#include <string>

namespace badbt {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string narrow(const wchar_t* w) {
    if (!w || !*w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

// Walk from the HID devnode up through its parents looking for an ID that embeds the
// device's Bluetooth MAC (e.g. BTHENUM\...\9&xxxx&0&AABBCCDDEEFF_C00000000).
bool find_address(DEVINST dn, uint64_t& address) {
    for (int depth = 0; depth < 6 && dn != 0; ++depth) {
        wchar_t id[MAX_DEVICE_ID_LEN] = {0};
        if (CM_Get_Device_IDW(dn, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
            break;
        }
        if (parse_bt_address(narrow(id), address)) {
            return true;
        }
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, dn, 0) != CR_SUCCESS) {
            break;
        }
        dn = parent;
    }
    return false;
}

} // namespace

DisconnectResult disconnect_bluetooth_device(const std::string& raw_input_path) {
    DisconnectResult res;

    const std::string instance_id = raw_input_path_to_instance_id(raw_input_path);
    if (instance_id.empty()) {
        res.detail = "no device path to disconnect";
        return res;
    }

    std::wstring winst = widen(instance_id);
    DEVINST devnode = 0;
    if (CM_Locate_DevNodeW(&devnode, const_cast<wchar_t*>(winst.c_str()),
                           CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        // Fail closed: if we cannot locate the device we cannot claim it was disconnected,
        // so the caller keeps input blocked until the user resets.
        res.detail = "device node not found for the reported device path";
        return res;
    }

    uint64_t address = 0;
    const bool have_address = find_address(devnode, address);

    bool hid_disabled = false;
    bool removed = false;
    bool radio_knew_device = false;

    if (have_address) {
        // HID service class {00001124-0000-1000-8000-00805F9B34FB}
        const GUID hid_service = {0x00001124, 0x0000, 0x1000,
                                  {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

        BLUETOOTH_FIND_RADIO_PARAMS params = {};
        params.dwSize = sizeof(params);
        HANDLE radio = nullptr;
        HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio(&params, &radio);
        if (find != nullptr) {
            do {
                BLUETOOTH_DEVICE_INFO info = {};
                info.dwSize = sizeof(info);
                info.Address.ullLong = address;
                if (BluetoothGetDeviceInfo(radio, &info) == ERROR_SUCCESS) {
                    radio_knew_device = true;
                    if (BluetoothSetServiceState(radio, &info, &hid_service,
                                                 BLUETOOTH_SERVICE_DISABLE) == ERROR_SUCCESS) {
                        hid_disabled = true;
                    }
                    if (BluetoothRemoveDevice(&info.Address) == ERROR_SUCCESS) {
                        removed = true;
                    }
                }
                CloseHandle(radio);
                radio = nullptr;
            } while (BluetoothFindNextRadio(find, &radio));
            BluetoothFindRadioClose(find);
        }
    }

    const std::string mac = have_address ? format_bt_address(address) : std::string("unknown MAC");

    if (removed || hid_disabled) {
        res.success = true;
        res.detail = std::string(removed ? "unpaired" : "HID service disabled") + " " + mac;
        if (removed && hid_disabled) {
            res.detail = "disconnected and unpaired " + mac;
        }
        return res;
    }

    // Fallback: disable the HID devnode. Needs administrator rights.
    if (CM_Disable_DevNode(devnode, 0) == CR_SUCCESS) {
        res.success = true;
        res.detail = "disabled HID device node for " + mac;
        return res;
    }

    if (!have_address) {
        res.detail = "could not determine the Bluetooth address of the device";
    } else if (!radio_knew_device) {
        res.detail = "no Bluetooth radio knows device " + mac;
    } else {
        res.detail = "Windows refused to disconnect " + mac + " (try running BadBT as administrator)";
    }
    return res;
}

} // namespace badbt

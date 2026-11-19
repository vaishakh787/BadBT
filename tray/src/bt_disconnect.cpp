#include "bt_disconnect.h"
#include "bt_address.h"
#include "bt_winrt.h"
#include "log.h"

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
bool find_address(DEVINST dn, uint64_t& address, bool verbose) {
    for (int depth = 0; depth < 8 && dn != 0; ++depth) {
        wchar_t id[MAX_DEVICE_ID_LEN] = {0};
        if (CM_Get_Device_IDW(dn, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
            break;
        }
        const std::string narrow_id = narrow(id);
        if (verbose) {
            log_line("  devnode[" + std::to_string(depth) + "] " + narrow_id);
        }
        if (parse_bt_address(narrow_id, address)) {
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

bool locate_devnode(const std::string& raw_input_path, DEVINST& devnode, std::string* instance_id_out) {
    const std::string instance_id = raw_input_path_to_instance_id(raw_input_path);
    if (instance_id_out) *instance_id_out = instance_id;
    if (instance_id.empty()) return false;
    std::wstring winst = widen(instance_id);
    return CM_Locate_DevNodeW(&devnode, const_cast<wchar_t*>(winst.c_str()),
                              CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS;
}

std::string win_err(DWORD code) { return std::to_string(static_cast<unsigned long>(code)); }

} // namespace

bool resolve_bt_address(const std::string& raw_input_path, uint64_t& address) {
    DEVINST devnode = 0;
    if (!locate_devnode(raw_input_path, devnode, nullptr)) {
        return false;
    }
    return find_address(devnode, address, false);
}

DisconnectResult disconnect_bluetooth_device(const std::string& raw_input_path) {
    DisconnectResult res;
    log_line("disconnect: start, raw path = " + raw_input_path);

    DEVINST devnode = 0;
    std::string instance_id;
    if (!locate_devnode(raw_input_path, devnode, &instance_id)) {
        // Fail closed: we cannot claim a disconnect for a device we cannot even find.
        res.detail = "device node not found for the reported device path";
        log_line("disconnect: FAILED - " + res.detail + " (instance id: " + instance_id + ")");
        return res;
    }
    log_line("disconnect: instance id = " + instance_id);

    uint64_t address = 0;
    const bool have_address = find_address(devnode, address, true);
    if (!have_address) {
        log_line("disconnect: no Bluetooth MAC found in the devnode chain");
    }

    bool hid_disabled = false;
    bool removed = false;
    bool le_unpaired = false;
    bool devnode_disabled = false;
    std::string mac = "unknown MAC";

    if (have_address) {
        mac = format_bt_address(address);
        res.mac_key = bt_address_key(address);
        log_line("disconnect: device MAC = " + mac);

        // ── Classic Bluetooth ──
        // HID service class {00001124-0000-1000-8000-00805F9B34FB}
        const GUID hid_service = {0x00001124, 0x0000, 0x1000,
                                  {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};

        int radios = 0;
        BLUETOOTH_FIND_RADIO_PARAMS params = {};
        params.dwSize = sizeof(params);
        HANDLE radio = nullptr;
        HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio(&params, &radio);
        if (find != nullptr) {
            do {
                ++radios;
                BLUETOOTH_DEVICE_INFO info = {};
                info.dwSize = sizeof(info);
                info.Address.ullLong = address;
                const DWORD known = BluetoothGetDeviceInfo(radio, &info);
                log_line("disconnect: radio " + std::to_string(radios) + " GetDeviceInfo -> " + win_err(known));
                if (known == ERROR_SUCCESS) {
                    const DWORD st = BluetoothSetServiceState(radio, &info, &hid_service,
                                                              BLUETOOTH_SERVICE_DISABLE);
                    log_line("disconnect: radio " + std::to_string(radios) + " SetServiceState(HID, disable) -> " + win_err(st));
                    if (st == ERROR_SUCCESS) hid_disabled = true;
                }
                CloseHandle(radio);
                radio = nullptr;
            } while (BluetoothFindNextRadio(find, &radio));
            BluetoothFindRadioClose(find);
        }
        log_line("disconnect: Bluetooth radios found = " + std::to_string(radios));

        // Needs only the address, so it also works when no radio "knows" the device.
        BLUETOOTH_ADDRESS bt_addr = {};
        bt_addr.ullLong = address;
        const DWORD rm = BluetoothRemoveDevice(&bt_addr);
        log_line("disconnect: BluetoothRemoveDevice -> " + win_err(rm));
        if (rm == ERROR_SUCCESS) removed = true;

        // ── Bluetooth LE (e.g. Flipper Zero as a BLE keyboard) ──
        if (!removed && !hid_disabled) {
            std::string le_detail;
            le_unpaired = winrt_unpair_le_device(address, le_detail);
            log_line("disconnect: " + le_detail);
        }
    }

    // ── Last resort: disable the HID devnode (administrator only) ──
    if (!removed && !hid_disabled && !le_unpaired) {
        const CONFIGRET cr = CM_Disable_DevNode(devnode, 0);
        log_line("disconnect: CM_Disable_DevNode -> CONFIGRET " + std::to_string(static_cast<unsigned long>(cr)));
        if (cr == CR_SUCCESS) devnode_disabled = true;
    }

    if (removed || hid_disabled || le_unpaired || devnode_disabled) {
        res.success = true;
        if (removed)               res.detail = "unpaired " + mac;
        else if (le_unpaired)      res.detail = "unpaired LE device " + mac;
        else if (hid_disabled)     res.detail = "HID service disabled for " + mac;
        else                       res.detail = "disabled HID device node for " + mac;
        log_line("disconnect: SUCCESS - " + res.detail);
        return res;
    }

    res.detail = have_address
        ? "Windows refused to disconnect " + mac + " (see badbt.log; running BadBT as administrator may help)"
        : "could not determine the Bluetooth address of the device";
    log_line("disconnect: FAILED - " + res.detail);
    return res;
}

} // namespace badbt

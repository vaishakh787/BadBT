// Portable unit tests for Raw Input path -> PnP instance ID -> Bluetooth MAC parsing.
// Pure C++ (no Win32); also builds with `clang++ -std=c++17` on macOS/Linux.
#include "bt_address.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n";                \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

int main() {
    using namespace badbt;

    // ── Raw Input path -> instance ID ──
    const std::string raw =
        "\\\\?\\HID#{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&09cc#9&2b1d3c2&0&0000#"
        "{4d1e55b2-f16f-11cf-88cb-001111000030}";
    CHECK(raw_input_path_to_instance_id(raw) ==
          "HID\\{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&09cc\\9&2b1d3c2&0&0000");
    CHECK(raw_input_path_to_instance_id("").empty());
    CHECK(raw_input_path_to_instance_id("\\??\\ACPI#PNP0303#4&1&0#{abc}") == "ACPI\\PNP0303\\4&1&0");

    // ── Address parsing ──
    uint64_t a = 0;

    // Classic HID parent instance ID.
    CHECK(parse_bt_address(
        "BTHENUM\\{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&09cc\\"
        "9&2b1d3c2&0&AABBCCDDEEFF_C00000000", a));
    CHECK(a == 0xAABBCCDDEEFFULL);

    // LE device node.
    a = 0;
    CHECK(parse_bt_address("BTHLE\\DEV_112233445566\\7&1a2b3c4d&0&112233445566", a));
    CHECK(a == 0x112233445566ULL);

    // Just DEV_ form, lowercase hex.
    a = 0;
    CHECK(parse_bt_address("BTHLE\\Dev_a1b2c3d4e5f6", a));
    CHECK(a == 0xA1B2C3D4E5F6ULL);

    // LE HID service form (address is the trailing _-delimited token).
    a = 0;
    CHECK(parse_bt_address(
        "BTHLEDEVICE\\{00001812-0000-1000-8000-00805f9b34fb}_DEV_VID&02045e_PID&0b0e_REV&0001_C0FFEE123456\\8&abc&0&0001", a));
    CHECK(a == 0xC0FFEE123456ULL);

    // GUID's last group is 12 hex but follows '-', so it must NOT be taken as an address.
    a = 0;
    CHECK(!parse_bt_address("HID\\{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_PID&09cc\\9&2b1d3c2&0&0000", a));

    // Non-Bluetooth device: no address.
    CHECK(!parse_bt_address("ACPI\\PNP0303\\4&1f2e3d&0", a));
    CHECK(!parse_bt_address("", a));

    // All-zero address rejected.
    CHECK(!parse_bt_address("BTHENUM\\DEV\\9&1&0&000000000000_C00000000", a));

    // Not exactly 12 hex digits -> rejected.
    CHECK(!parse_bt_address("BTHENUM\\DEV\\9&1&0&AABBCCDDEEF_C00000000", a));
    CHECK(!parse_bt_address("BTHENUM\\DEV\\9&1&0&AABBCCDDEEFF0_C00000000", a));

    // ── Formatting ──
    CHECK(format_bt_address(0xAABBCCDDEEFFULL) == "AA:BB:CC:DD:EE:FF");
    CHECK(format_bt_address(0x0102030405FFULL) == "01:02:03:04:05:FF");

    if (g_failures == 0) {
        std::cout << "test_bt_address: ALL PASSED\n";
        return 0;
    }
    std::cerr << "test_bt_address: " << g_failures << " FAILED\n";
    return 1;
}

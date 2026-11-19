#include "bt_address.h"

#include <cctype>
#include <cstdio>

namespace badbt {

std::string raw_input_path_to_instance_id(const std::string& raw_path) {
    std::string s = raw_path;
    if (s.empty()) return "";

    // Strip the "\\?\" (or "\??\") device-interface prefix.
    if (s.size() >= 4 && s[0] == '\\' && s[1] == '\\' && s[2] == '?' && s[3] == '\\') {
        s.erase(0, 4);
    } else if (s.size() >= 4 && s[0] == '\\' && s[1] == '?' && s[2] == '?' && s[3] == '\\') {
        s.erase(0, 4);
    }

    // Strip the trailing "#{interface-class-guid}" suffix.
    size_t guid_pos = s.rfind("#{");
    if (guid_pos != std::string::npos && guid_pos > 0) {
        s.erase(guid_pos);
    }

    for (char& c : s) {
        if (c == '#') c = '\\';
    }
    return s;
}

static bool is_hex(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

bool parse_bt_address(const std::string& instance_id, uint64_t& address) {
    const size_t n = instance_id.size();
    size_t i = 0;
    while (i < n) {
        if (!is_hex(instance_id[i])) {
            ++i;
            continue;
        }
        // Maximal run of hex digits [i, j).
        size_t j = i;
        while (j < n && is_hex(instance_id[j])) ++j;

        const bool exact12 = (j - i) == 12;
        // Bordered the way Windows writes it: preceded by '&' or '_' (this deliberately
        // excludes the last group of a GUID, which follows '-') and followed by '_', '\\'
        // or the end of the string.
        const bool left_ok = i > 0 && (instance_id[i - 1] == '&' || instance_id[i - 1] == '_');
        const bool right_ok = j == n || instance_id[j] == '_' || instance_id[j] == '\\' || instance_id[j] == '&';

        if (exact12 && left_ok && right_ok) {
            uint64_t value = 0;
            for (size_t k = i; k < j; ++k) {
                char c = instance_id[k];
                int d = (c >= '0' && c <= '9') ? c - '0'
                      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                      : c - 'A' + 10;
                value = (value << 4) | static_cast<uint64_t>(d);
            }
            if (value != 0) {
                address = value;
                return true;
            }
        }
        i = j;
    }
    return false;
}

std::string bt_address_key(uint64_t address) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "bt:%012llX",
                  static_cast<unsigned long long>(address & 0xFFFFFFFFFFFFULL));
    return buf;
}

std::string format_bt_address(uint64_t address) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  static_cast<unsigned>((address >> 40) & 0xFF),
                  static_cast<unsigned>((address >> 32) & 0xFF),
                  static_cast<unsigned>((address >> 24) & 0xFF),
                  static_cast<unsigned>((address >> 16) & 0xFF),
                  static_cast<unsigned>((address >> 8) & 0xFF),
                  static_cast<unsigned>(address & 0xFF));
    return buf;
}

} // namespace badbt

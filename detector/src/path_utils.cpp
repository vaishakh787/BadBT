#include "path_utils.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <iostream>

namespace badbt {

std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
#if defined(_WIN32)
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &result[0], size);
    return result;
#else
    return std::wstring(str.begin(), str.end());
#endif
}

bool file_exists(const std::string& path) {
#if defined(_WIN32)
    std::wstring wpath = utf8_to_wstring(path);
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return false;
#endif
}

std::string get_executable_directory() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return ".";
    }
    std::wstring path(buffer, len);
    size_t last_slash = path.find_last_of(L"\\/");
    if (last_slash != std::wstring::npos) {
        return wstring_to_utf8(path.substr(0, last_slash));
    }
    return ".";
#else
    return ".";
#endif
}

std::string find_model_path(const std::string& preferred_path) {
    if (!preferred_path.empty() && file_exists(preferred_path)) {
        return preferred_path;
    }

    std::string exe_dir = get_executable_directory();
    std::vector<std::string> candidates = {
        exe_dir + "\\model\\rf_model.onnx",
        exe_dir + "/model/rf_model.onnx",
        exe_dir + "\\rf_model.onnx",
        exe_dir + "/rf_model.onnx",
        exe_dir + "\\..\\..\\model\\rf_model.onnx",
        exe_dir + "/../../model/rf_model.onnx",
        exe_dir + "\\..\\model\\rf_model.onnx",
        exe_dir + "/../model/rf_model.onnx",
        "model/rf_model.onnx",
        "model\\rf_model.onnx",
        "rf_model.onnx"
    };

    for (const auto& cand : candidates) {
        if (file_exists(cand)) {
            return cand;
        }
    }

    return preferred_path.empty() ? "model/rf_model.onnx" : preferred_path;
}

} // namespace badbt

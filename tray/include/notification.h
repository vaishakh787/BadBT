#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <string>

namespace badbt {

// Displays a native Windows balloon / toast notification via the taskbar notification area
bool show_tray_notification(
    HWND hwnd,
    UINT icon_id,
    const std::wstring& title,
    const std::wstring& message,
    DWORD info_flags = NIIF_WARNING | NIIF_LARGE_ICON
);

} // namespace badbt

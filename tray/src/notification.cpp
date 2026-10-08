#include "notification.h"
#include <cwchar>

namespace badbt {

bool show_tray_notification(
    HWND hwnd,
    UINT icon_id,
    const std::wstring& title,
    const std::wstring& message,
    DWORD info_flags
) {
    if (!hwnd) return false;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = icon_id;
    nid.uFlags = NIF_INFO;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, message.c_str(), _TRUNCATE);
    nid.dwInfoFlags = info_flags;
    nid.uTimeout = 10000; // Hint to shell (10 seconds)

    return Shell_NotifyIconW(NIM_MODIFY, &nid) == TRUE;
}

} // namespace badbt

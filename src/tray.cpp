#include "arc/tray.h"

#include <shellapi.h>

namespace {
constexpr UINT WM_TRAYICON = WM_APP + 1;
NOTIFYICONDATAW g_nid{};

HMENU create_tray_menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Exit");
    return menu;
}  // namespace

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            HMENU menu = create_tray_menu();
            UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if (cmd == 1) {
                PostQuitMessage(0);
            }
        }
        break;
    }
    case WM_DESTROY:
        return 0;
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
}  // namespace

namespace arc {

HWND tray_init(HINSTANCE hInstance, const std::wstring &tooltip) {
    const wchar_t *kClassName = L"AltRightClickTrayWindow";
    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kClassName, L"AltRightClick", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hInstance, nullptr);

    // Prepare tray icon data
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);  // Replace with custom icon if available
    wcsncpy_s(g_nid.szTip, tooltip.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_ADD, &g_nid);
    return hwnd;
}

void tray_cleanup(HWND hwnd) {
    if (g_nid.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_nid = NOTIFYICONDATAW{};
    }
    if (hwnd)
        DestroyWindow(hwnd);
}

}  // namespace arc

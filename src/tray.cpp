#include "arc/tray.h"

#include <shellapi.h>

#include <algorithm>

#include "arc/config.h"
#include "arc/hook.h"

namespace {
constexpr UINT WM_TRAYICON = WM_APP + 1;
NOTIFYICONDATAW g_nid{};

enum MenuId : UINT {
    kMenuExit = 1,
    kMenuClickTimeInc = 100,
    kMenuClickTimeDec = 101,
    kMenuMoveRadiusInc = 102,
    kMenuMoveRadiusDec = 103,
    kMenuToggleIgnoreInjected = 104,
    kMenuSaveConfig = 105,
};

HMENU create_tray_menu(const arc::TrayContext* ctx) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuClickTimeInc, L"Click Time +10 ms");
    AppendMenuW(menu, MF_STRING, kMenuClickTimeDec, L"Click Time -10 ms");
    AppendMenuW(menu, MF_STRING, kMenuMoveRadiusInc, L"Move Radius +1 px");
    AppendMenuW(menu, MF_STRING, kMenuMoveRadiusDec, L"Move Radius -1 px");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    std::wstring inj = L"Ignore Injected: ";
    inj += (ctx && ctx->cfg && ctx->cfg->ignore_injected) ? L"ON" : L"OFF";
    AppendMenuW(menu, MF_STRING, kMenuToggleIgnoreInjected, inj.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSaveConfig, L"Save Settings");
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
    return menu;
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            auto* ctx = reinterpret_cast<arc::TrayContext*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
            HMENU menu = create_tray_menu(ctx);
            UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if (ctx && ctx->cfg) {
                switch (cmd) {
                case kMenuClickTimeInc:
                    ctx->cfg->click_time_ms =
                        static_cast<unsigned int>(std::min<int>(ctx->cfg->click_time_ms + 10, 5000));
                    arc::apply_hook_config(*ctx->cfg);
                    break;
                case kMenuClickTimeDec:
                    ctx->cfg->click_time_ms = static_cast<unsigned int>(
                        std::max<int>(static_cast<int>(ctx->cfg->click_time_ms) - 10, 10));
                    arc::apply_hook_config(*ctx->cfg);
                    break;
                case kMenuMoveRadiusInc:
                    ctx->cfg->move_radius_px = std::min(ctx->cfg->move_radius_px + 1, 100);
                    arc::apply_hook_config(*ctx->cfg);
                    break;
                case kMenuMoveRadiusDec:
                    ctx->cfg->move_radius_px = std::max(ctx->cfg->move_radius_px - 1, 0);
                    arc::apply_hook_config(*ctx->cfg);
                    break;
                case kMenuToggleIgnoreInjected:
                    ctx->cfg->ignore_injected = !ctx->cfg->ignore_injected;
                    arc::apply_hook_config(*ctx->cfg);
                    break;
                case kMenuSaveConfig:
                    if (ctx->config_path && !ctx->config_path->empty()) {
                        arc::save_config(*ctx->config_path, *ctx->cfg);
                    }
                    break;
                case kMenuExit:
                    PostQuitMessage(0);
                    break;
                default:
                    break;
                }
            } else if (cmd == kMenuExit) {
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

HWND tray_init(HINSTANCE hInstance, const std::wstring &tooltip, arc::TrayContext* ctx) {
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

    if (ctx) {
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
    }

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

/**
 * @file tray.cpp
 * @brief System tray icon/window and worker thread implementation.
 */

#include "arc/tray.h"

#include <shellapi.h>

#include <algorithm>
#include <string>
#include <thread>
#include <filesystem>

#include "arc/config.h"
#include "arc/hook.h"
#include "arc/log.h"
#include "arc/persistence.h"

namespace {
/// Custom window message used by the tray icon callback.
constexpr UINT WM_TRAYICON = WM_APP + 1;
/// Last registered tray icon data (owner HWND, callbacks, etc.).
NOTIFYICONDATAW g_nid{};
/// Worker thread hosting the tray window/message loop.
static std::thread g_trayThread;
/// Thread id of the tray worker for PostThreadMessage/WM_QUIT.
static DWORD g_trayThreadId = 0;

/**
 * @brief Command identifiers for the tray popup menu.
 */
enum MenuId : UINT {
    kMenuExit = 1,                  ///< Exit application.
    kMenuToggleEnabled = 50,        ///< Toggle enable/disable.
    kMenuClickTimeInc = 100,        ///< Increase click time.
    kMenuClickTimeDec = 101,        ///< Decrease click time.
    kMenuMoveRadiusInc = 102,       ///< Increase movement radius.
    kMenuMoveRadiusDec = 103,       ///< Decrease movement radius.
    kMenuToggleIgnoreInjected = 104,  ///< Toggle ignore injected events.
    kMenuSaveConfig = 105,          ///< Save settings to config file.
    kMenuOpenConfigFolder = 106,    ///< Open folder containing config file.
    kMenuTogglePersistence = 107,   ///< Toggle persistence monitor.
};

/**
 * @brief Builds a context menu reflecting current configuration.
 *
 * @param ctx Live tray context (may be null, in which case a minimal menu is built).
 * @return Created popup menu; caller must destroy via DestroyMenu.
 */
HMENU create_tray_menu(const arc::tray::TrayContext *ctx) {
    HMENU menu = CreatePopupMenu();
    std::wstring en = L"Enabled: ";
    en += (ctx && ctx->cfg.enabled) ? L"ON" : L"OFF";
    AppendMenuW(menu, MF_STRING, kMenuToggleEnabled, en.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuClickTimeInc, L"Click Time +10 ms");
    AppendMenuW(menu, MF_STRING, kMenuClickTimeDec, L"Click Time -10 ms");
    AppendMenuW(menu, MF_STRING, kMenuMoveRadiusInc, L"Move Radius +1 px");
    AppendMenuW(menu, MF_STRING, kMenuMoveRadiusDec, L"Move Radius -1 px");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    std::wstring inj = L"Ignore Injected: ";
    inj += (ctx && ctx->cfg.ignore_injected) ? L"ON" : L"OFF";
    AppendMenuW(menu, MF_STRING, kMenuToggleIgnoreInjected, inj.c_str());
    std::wstring per = L"Persistence Monitor: ";
    per += (ctx && ctx->cfg.persistence_enabled) ? L"ON" : L"OFF";
    AppendMenuW(menu, MF_STRING, kMenuTogglePersistence, per.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSaveConfig, L"Save Settings");
    AppendMenuW(menu, MF_STRING, kMenuOpenConfigFolder, L"Open Config Folder");
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
    return menu;
}

/** Converts a UTF-8 string to UTF-16. */
static std::wstring to_w(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

/** Returns directory part of a path (or the input if no separator is found). */
static std::wstring dir_of(const std::wstring &path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return path;
    return path.substr(0, pos);
}

/** Returns the full path of the current executable. */
static std::wstring get_module_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf);
}

/** Hidden tray window procedure handling icon/menu interactions. */
LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_QUERYENDSESSION:
    case WM_ENDSESSION: {
        auto *ctx = reinterpret_cast<arc::tray::TrayContext *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (ctx) {
            ctx->exit_requested.store(true);
        }
        PostQuitMessage(0);
        return TRUE;
    }
    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            auto *ctx = reinterpret_cast<arc::tray::TrayContext *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
            HMENU menu = create_tray_menu(ctx);
            UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if (ctx) {
                switch (cmd) {
                case kMenuToggleEnabled:
                    ctx->cfg.enabled = !ctx->cfg.enabled;
                    arc::hook::apply_hook_config(ctx->cfg);
                    arc::tray::notify(L"altrightclick", ctx->cfg.enabled ? L"Enabled" : L"Disabled");
                    break;
                case kMenuClickTimeInc:
                    ctx->cfg.click_time_ms =
                        static_cast<unsigned int>(std::min<int>(ctx->cfg.click_time_ms + 10, 5000));
                    arc::hook::apply_hook_config(ctx->cfg);
                    break;
                case kMenuClickTimeDec:
                    ctx->cfg.click_time_ms =
                        static_cast<unsigned int>(std::max<int>(static_cast<int>(ctx->cfg.click_time_ms) - 10, 10));
                    arc::hook::apply_hook_config(ctx->cfg);
                    break;
                case kMenuMoveRadiusInc:
                    ctx->cfg.move_radius_px =
                        static_cast<unsigned int>(std::min<int>(static_cast<int>(ctx->cfg.move_radius_px) + 1, 100));
                    arc::hook::apply_hook_config(ctx->cfg);
                    break;
                case kMenuMoveRadiusDec:
                    ctx->cfg.move_radius_px =
                        static_cast<unsigned int>(std::max<int>(static_cast<int>(ctx->cfg.move_radius_px) - 1, 0));
                    arc::hook::apply_hook_config(ctx->cfg);
                    break;
                case kMenuToggleIgnoreInjected:
                    ctx->cfg.ignore_injected = !ctx->cfg.ignore_injected;
                    arc::hook::apply_hook_config(ctx->cfg);
                    break;
                case kMenuTogglePersistence: {
                    bool was = ctx->cfg.persistence_enabled;
                    ctx->cfg.persistence_enabled = !ctx->cfg.persistence_enabled;
                    if (!was && ctx->cfg.persistence_enabled) {
                        // Spawn monitor now so it can restart us if we crash later
                        std::wstring exe = get_module_path();
                        std::string cfgPath = ctx->config_path.u8string();
                        arc::persistence::spawn_monitor(exe, cfgPath);
                        arc::tray::notify(L"altrightclick", L"Persistence monitor enabled");
                    } else if (was && !ctx->cfg.persistence_enabled) {
                        bool stopped = arc::persistence::stop_monitor_graceful(3000);
                        arc::tray::notify(L"altrightclick", stopped ? L"Persistence monitor stopped" : L"No monitor running");
                    }
                    break;
                }
                case kMenuSaveConfig:
                    if (!ctx->config_path.empty()) {
                        arc::config::save(ctx->config_path, ctx->cfg);
                    }
                    break;
                case kMenuOpenConfigFolder: {
                    std::filesystem::path dir = ctx->config_path.parent_path();
                    std::wstring wdir = dir.wstring();
                    ShellExecuteW(nullptr, L"open", wdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                }
                case kMenuExit:
                    ctx->exit_requested.store(true);
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

namespace arc::tray {

/**
 * Creates a hidden window, registers the tray icon, and stores ctx.
 */
HWND init(HINSTANCE hInstance, const std::wstring &tooltip, TrayContext *ctx) {
    const wchar_t *kClassName = L"AltRightClickTrayWindow";
    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);

    RegisterClassExW(&wc);

    // Create a hidden tool window (no taskbar button), used only to receive tray messages.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"AltRightClick", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                hInstance, nullptr);

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

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        arc::log::error("Shell_NotifyIconW(NIM_ADD) failed");
    }
    return hwnd;
}

/** Removes the tray icon and destroys the hidden window. */
void cleanup(HWND hwnd) {
    if (g_nid.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_nid = NOTIFYICONDATAW{};
    }
    if (hwnd)
        DestroyWindow(hwnd);
}

}  // namespace arc::tray

namespace arc::tray {

/** Starts the tray worker thread and pumps a private message loop. */
bool start(const std::wstring &tooltip, TrayContext *ctx) {
    if (g_trayThread.joinable())
        return true;
    g_trayThread = std::thread([tooltip, ctx]() {
        g_trayThreadId = GetCurrentThreadId();
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        HWND hwnd = init(hInst, tooltip, ctx);
        if (!hwnd) {
            arc::log::error("Tray worker: failed to create tray window");
            return;
        }
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        cleanup(hwnd);
        g_trayThreadId = 0;
    });
    return true;
}

/** Posts WM_QUIT to the tray thread and joins it. */
void stop() {
    if (g_trayThreadId)
        PostThreadMessage(g_trayThreadId, WM_QUIT, 0, 0);
    if (g_trayThread.joinable())
        g_trayThread.join();
}

/** Shows a balloon notification using the tray icon. */
void notify(const std::wstring &title, const std::wstring &message) {
    if (!g_nid.hWnd)
        return;
    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags |= NIF_INFO;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, message.c_str(), _TRUNCATE);
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

}  // namespace arc::tray

/**
 * @file tray.cpp
 * @brief System tray icon/window and worker thread implementation.
 *
 * This file implements a hidden window which receives tray icon messages,
 * builds and shows a context menu reflecting the application's runtime
 * configuration, and exposes functions to start/stop a dedicated tray
 * worker thread and send balloon notifications.
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

/**
 * @brief Global NOTIFYICONDATAW instance describing the currently registered
 *        tray icon.
 *
 * This holds the last data used when registering the icon with Shell_NotifyIconW
 * so callers can update or delete the icon later. Access is only from the tray
 * worker thread.
 */
NOTIFYICONDATAW g_nid{};

/**
 * @brief Worker thread hosting the tray window/message loop.
 *
 * This thread is created by arc::tray::start() and joined by arc::tray::stop().
 */
static std::thread g_trayThread;

/**
 * @brief Thread id of the tray worker for PostThreadMessage/WM_QUIT.
 *
 * Stored so other threads may post WM_QUIT to the tray thread when shutting down.
 */
static DWORD g_trayThreadId = 0;

/**
 * @brief Command identifiers for the tray popup menu.
 *
 * Values are chosen to avoid collisions with system/reserved values.
 */
enum MenuId : UINT {
    kMenuExit = 1,                    ///< Exit application.
    kMenuToggleEnabled = 50,          ///< Toggle enable/disable.
    kMenuClickTimeInc = 100,          ///< Increase click time.
    kMenuClickTimeDec = 101,          ///< Decrease click time.
    kMenuMoveRadiusInc = 102,         ///< Increase movement radius.
    kMenuMoveRadiusDec = 103,         ///< Decrease movement radius.
    kMenuToggleIgnoreInjected = 104,  ///< Toggle ignore injected events.
    kMenuSaveConfig = 105,            ///< Save settings to config file.
    kMenuOpenConfigFolder = 106,      ///< Open folder containing config file.
    kMenuTogglePersistence = 107,     ///< Toggle persistence monitor.
};

/**
 * @brief Builds a context menu reflecting current configuration.
 *
 * The returned HMENU must be destroyed by the caller using DestroyMenu().
 *
 * @param ctx Live tray context (may be null, in which case a minimal menu is built).
 * @return Created popup menu handle. Caller is responsible for DestroyMenu(menu).
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
    bool enabled = (ctx && ctx->cfg.persistence_enabled);
    per += enabled ? L"ON" : L"OFF";
    bool running = arc::persistence::is_monitor_running();
    per += running ? L" (running)" : L" (stopped)";
    AppendMenuW(menu, MF_STRING, kMenuTogglePersistence, per.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSaveConfig, L"Save Settings");
    AppendMenuW(menu, MF_STRING, kMenuOpenConfigFolder, L"Open Config Folder");
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
    return menu;
}

/**
 * @brief Persist configuration changes driven from the tray menu.
 */
static void persist_config_if_possible(const arc::tray::TrayContext *ctx) {
    if (!ctx || ctx->config_path.empty())
        return;
    if (!arc::config::save(ctx->config_path, ctx->cfg)) {
        arc::log::error("Tray: failed to save configuration to " + ctx->config_path.u8string());
        arc::tray::notify(L"altrightclick", L"Failed to save config. Check disk permissions.");
    }
}

/**
 * @brief Convert a UTF-8 encoded std::string to a UTF-16 std::wstring.
 *
 * Uses the Win32 API MultiByteToWideChar with code page CP_UTF8 to perform the
 * conversion. The returned wstring does not contain a terminating null
 * character (std::wstring manages the size). An empty input string yields an
 * empty output string.
 *
 * @param s UTF-8 encoded input string to convert.
 * @return std::wstring Converted UTF-16 string. If conversion fails an empty
 *         string is returned.
 */
static std::wstring to_w(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

/**
 * @brief Extract directory component from a path.
 *
 * Searches for the last path separator (either '\\' or '/') and returns the
 * substring up to but not including that separator. If no separator is found
 * the original input is returned unchanged.
 *
 * This helper does not perform any normalization; it only performs a simple
 * textual split.
 *
 * @param path Input path as a UTF-16 string.
 * @return std::wstring Directory portion of the path or the original string
 *         if no separator is present.
 */
static std::wstring dir_of(const std::wstring &path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return path;
    return path.substr(0, pos);
}

/**
 * @brief Retrieve the full path to the current executable module.
 *
 * Wrapper around GetModuleFileNameW(nullptr, ...). The returned string will
 * contain the absolute path to the running executable. If the Win32 call
 * fails, the returned std::wstring may be empty or contain a partial result.
 *
 * @note Caller should be prepared to handle an empty result if GetModuleFileNameW
 *       fails due to an unusual environment or insufficient buffer size.
 *
 * @return std::wstring Absolute path to the current executable (UTF-16).
 */
static std::wstring get_module_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf);
}

/**
 * @brief Hidden tray window procedure handling icon/menu interactions.
 *
 * This window procedure receives messages for the hidden tray window used to
 * present the application's context menu and handle queries for session end.
 * It expects the TrayContext pointer to be stored in GWLP_USERDATA on the
 * window.
 *
 * @param hwnd Window handle receiving the message.
 * @param msg Message identifier.
 * @param wParam Additional message information (depends on msg).
 * @param lParam Additional message information (depends on msg).
 * @return LRESULT message result (DefWindowProc is called for unhandled messages).
 */
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
                    persist_config_if_possible(ctx);
                    arc::tray::notify(L"altrightclick", ctx->cfg.enabled ? L"Enabled" : L"Disabled");
                    break;
                case kMenuClickTimeInc:
                    ctx->cfg.click_time_ms =
                        static_cast<unsigned int>(std::min<int>(ctx->cfg.click_time_ms + 10, 5000));
                    arc::hook::apply_hook_config(ctx->cfg);
                    persist_config_if_possible(ctx);
                    break;
                case kMenuClickTimeDec:
                    ctx->cfg.click_time_ms =
                        static_cast<unsigned int>(std::max<int>(static_cast<int>(ctx->cfg.click_time_ms) - 10, 10));
                    arc::hook::apply_hook_config(ctx->cfg);
                    persist_config_if_possible(ctx);
                    break;
                case kMenuMoveRadiusInc:
                    ctx->cfg.move_radius_px =
                        static_cast<unsigned int>(std::min<int>(static_cast<int>(ctx->cfg.move_radius_px) + 1, 100));
                    arc::hook::apply_hook_config(ctx->cfg);
                    persist_config_if_possible(ctx);
                    break;
                case kMenuMoveRadiusDec:
                    ctx->cfg.move_radius_px =
                        static_cast<unsigned int>(std::max<int>(static_cast<int>(ctx->cfg.move_radius_px) - 1, 0));
                    arc::hook::apply_hook_config(ctx->cfg);
                    persist_config_if_possible(ctx);
                    break;
                case kMenuToggleIgnoreInjected:
                    ctx->cfg.ignore_injected = !ctx->cfg.ignore_injected;
                    arc::hook::apply_hook_config(ctx->cfg);
                    persist_config_if_possible(ctx);
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
                        unsigned int to = static_cast<unsigned int>(ctx->cfg.persistence_stop_timeout_ms);
                        bool stopped = arc::persistence::stop_monitor_graceful(to);
                        arc::tray::notify(L"altrightclick",
                                          stopped ? L"Persistence monitor stopped" : L"No monitor running");
                    }
                    persist_config_if_possible(ctx);
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
 * @brief Creates a hidden window, registers the tray icon, and stores ctx.
 *
 * This function registers a window class, creates a hidden tool window used to
 * receive notifications from the system tray icon, and adds the icon to the
 * shell using Shell_NotifyIconW(NIM_ADD). The provided @p ctx pointer is stored
 * in the window's GWLP_USERDATA slot so the window proc can access runtime
 * configuration and state.
 *
 * @param hInstance Module instance handle (typically from WinMain or GetModuleHandleW).
 * @param tooltip UTF-16 tooltip text to display for the tray icon.
 * @param ctx Pointer to a TrayContext containing runtime state. May be null.
 * @return HWND handle of the created window or nullptr on failure.
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

/**
 * @brief Removes the tray icon and destroys the hidden window.
 *
 * If a tray icon is registered (g_nid.hWnd non-null) this function will call
 * Shell_NotifyIconW(NIM_DELETE) to remove it. The provided @p hwnd is then
 * destroyed with DestroyWindow(). This may be called from the tray thread when
 * exiting.
 *
 * @param hwnd Handle to the hidden tray window to destroy. May be null.
 */
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

/**
 * @brief Starts the tray worker thread and pumps a private message loop.
 *
 * This spawns a new thread that creates the hidden tray window and enters a
 * standard Windows message loop (GetMessage/TranslateMessage/DispatchMessage).
 * The tray thread id is stored in a global so other threads can post WM_QUIT to
 * request shutdown.
 *
 * @param tooltip Tooltip text for the tray icon shown to the user.
 * @param ctx Pointer to a TrayContext providing runtime configuration and state.
 * @return true if the worker thread was started or already running; false on failure.
 */
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

/**
 * @brief Posts WM_QUIT to the tray thread and joins it.
 *
 * If the tray worker thread is running this function will attempt to post a
 * WM_QUIT message to cause the message loop to exit, then join the thread. It is
 * safe to call from any thread. After joining, the stored thread id is cleared.
 */
void stop() {
    if (g_trayThreadId)
        PostThreadMessage(g_trayThreadId, WM_QUIT, 0, 0);
    if (g_trayThread.joinable())
        g_trayThread.join();
}

/**
 * @brief Shows a balloon notification using the tray icon.
 *
 * Constructs a NOTIFYICONDATAW based on the last registered tray icon and sets
 * the NIF_INFO flag to display a balloon (toast) notification with the given
 * title and message. If no icon is registered, this function returns silently.
 *
 * @param title Title text for the balloon (UTF-16).
 * @param message Body text for the balloon (UTF-16).
 */
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

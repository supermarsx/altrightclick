/**
 * @file tray.h
 * @brief System tray integration and worker thread API.
 *
 * Exposes a small API to create and manage a notification area (system tray)
 * icon on Windows. The tray UI is hosted on a dedicated thread with its own
 * window/message loop so the main controller thread remains responsive.
 */
/**
 * @file tray.h
 * @brief System tray integration and worker thread API.
 *
 * Exposes a small API to create and manage a notification area (system tray)
 * icon on Windows. The tray UI is hosted on a dedicated thread with its own
 * window/message loop so the main controller thread remains responsive.
 */
#pragma once

#include <windows.h>
#include <string>
#include <atomic>
#include <filesystem>

namespace arc { namespace config { struct Config; } }
namespace arc { namespace tray {

/**
 * @brief Live context passed to the tray worker.
 *
 * Holds references to lifetime-managed state owned by the main controller.
 */
struct TrayContext {
    /** Active runtime configuration to reflect and mutate from the tray. */
    arc::config::Config &cfg;
    /** Config file path for Save/Open actions. */
    const std::filesystem::path &config_path;
    /** Stop signal; set to true when user clicks Exit in the tray. */
    std::atomic<bool> &exit_requested;
};

/**
 * @brief Initializes a hidden tray window and adds the tray icon.
 *
 * @param hInstance Module handle.
 * @param tooltip   Tooltip text for the tray icon.
 * @param ctx       Optional live context (stored in window user-data).
 * @return HWND of the hidden tray window owning the icon, or nullptr on error.
 */
HWND init(HINSTANCE hInstance, const std::wstring &tooltip, TrayContext *ctx);

/**
 * @brief Starts the tray worker thread.
 *
 * Spawns a new thread, creates the tray window/icon and pumps a private message
 * loop until @ref stop posts WM_QUIT. Returns immediately.
 */
bool start(const std::wstring &tooltip, TrayContext *ctx);

/**
 * @brief Requests tray worker shutdown and joins the thread.
 */
void stop();

/**
 * @brief Shows a brief notification balloon from the tray icon.
 * @note No-op if the tray icon has not been initialized.
 */
void notify(const std::wstring &title, const std::wstring &message);

/**
 * @brief Removes the tray icon and destroys the hidden window.
 */
void cleanup(HWND hwnd);

}  // namespace tray

}  // namespace arc

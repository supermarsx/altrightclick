/**
 * @file hook.h
 * @brief Low-level mouse hook and worker thread.
 *
 * Provides APIs to install/uninstall the WH_MOUSE_LL hook and to run it inside
 * a dedicated worker thread with a private message loop. The hook translates a
 * configurable source click (e.g., Alt+Left) into a right-click, with
 * discrimination between click and drag.
 */
#pragma once

#include <windows.h>

namespace arc { namespace config { struct Config; } }

namespace arc { namespace hook {

/**
 * @brief Installs the process-wide low-level mouse hook.
 *
 * Typically called from the hook worker thread; not intended for UI threads as
 * it requires a message loop. Safe to call if already installed (no-op).
 *
 * @return true if the hook was successfully installed.
 */
bool install();

/**
 * @brief Removes the low-level mouse hook if installed.
 *
 * Safe to call multiple times; subsequent calls after removal are no-ops.
 */
void remove();

/**
 * @brief Low-level mouse hook procedure.
 *
 * @param nCode Hook code from WH_MOUSE_LL contract.
 * @param wParam Mouse message identifier (e.g., WM_LBUTTONDOWN).
 * @param lParam Pointer to MSLLHOOKSTRUCT for the event.
 * @return Either 1 to consume the event or result of CallNextHookEx.
 */
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

/**
 * @brief Applies runtime configuration for the hook.
 *
 * Thread-safe: internal state uses atomics and private storage.
 *
 * @param cfg New configuration values to apply.
 */
void apply_hook_config(const arc::config::Config &cfg);

/**
 * @brief Starts the hook worker thread and installs the hook.
 *
 * The worker pumps a private message loop until @ref stop posts a
 * quit message. The call blocks until installation succeeds/fails, then returns.
 *
 * @return true if the hook was installed and the worker is running.
 */
bool start();

/**
 * @brief Requests the hook worker to quit and waits for it to join.
 */
void stop();

}  // namespace hook

}  // namespace arc

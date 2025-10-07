/**
 * @file app.h
 * @brief Application helpers: Windows message loop utilities.
 *
 * Declares a minimal helper to run a standard Windows message loop, optionally
 * watching a virtual-key to terminate the loop. Higher-level code (main/service)
 * uses dedicated worker threads for the hook and tray; the controller thread
 * remains responsive and coordinates shutdown.
 */
#pragma once

namespace arc { namespace app {

/**
 * @brief Runs a standard Windows message loop.
 *
 * The loop terminates when WM_QUIT is received. If @p exit_vk is non-zero, the
 * state of that virtual key is polled periodically to allow early exit.
 *
 * @param exit_vk Optional virtual-key code to enable keyboard exit (e.g., VK_ESCAPE).
 * @return 0 on normal shutdown.
 */
int run_loop(unsigned int exit_vk = 0x1B);

}  // namespace app

}  // namespace arc

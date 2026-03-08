/**
 * @file app.h
 * @brief Application helpers: Windows message loop utilities.
 *
 * Declares a minimal helper to run a standard Windows message loop, optionally
 * watching a virtual-key combo to terminate the loop. Higher-level code
 * (main/service) uses dedicated worker threads for the hook and tray; the
 * controller thread remains responsive and coordinates shutdown.
 */
#pragma once

#include <vector>

namespace arc { namespace app {

/**
 * @brief Runs a standard Windows message loop.
 *
 * The loop terminates when WM_QUIT is received. If @p exit_combo is non-empty,
 * the state of every key in the combo is polled; when all are pressed
 * simultaneously the loop exits early (e.g., {VK_ESCAPE} for a single key or
 * {VK_CONTROL, VK_MENU, 'Q'} for Ctrl+Alt+Q).
 *
 * @param exit_combo Optional virtual-key combo for keyboard exit.
 * @return 0 on normal shutdown.
 */
int run_loop(const std::vector<unsigned int> &exit_combo = {0x1B});

}  // namespace app

}  // namespace arc

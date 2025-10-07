/**
 * @file app.cpp
 * @brief Implementation of simple Windows message loop helper.
 */

#include "arc/app.h"

#include <windows.h>
#include "arc/config.h"

namespace arc::app {

/**
 * Runs the process message loop until WM_QUIT or optional keyboard exit.
 *
 * @param exit_vk Optional virtual-key to allow early exit (0 to disable).
 * @return 0 on normal shutdown.
 */
int run_loop(unsigned int exit_vk) {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (exit_vk && (GetAsyncKeyState(static_cast<int>(exit_vk)) & 0x8000)) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

}  // namespace arc::app

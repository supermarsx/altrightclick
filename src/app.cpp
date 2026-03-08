/**
 * @file app.cpp
 * @brief Implementation of simple Windows message loop helper.
 */

#include "arc/app.h"

#include <windows.h>
#include "arc/config.h"

namespace arc::app {

/**
 * Returns true if every virtual key in the combo is currently pressed.
 */
static bool is_combo_pressed(const std::vector<unsigned int> &combo) {
    for (auto vk : combo) {
        if ((GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) == 0)
            return false;
    }
    return true;
}

/**
 * Runs the process message loop until WM_QUIT or optional keyboard exit.
 *
 * @param exit_combo Optional virtual-key combo to allow early exit (empty to
 *                   disable).
 * @return 0 on normal shutdown.
 */
int run_loop(const std::vector<unsigned int> &exit_combo) {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!exit_combo.empty() && is_combo_pressed(exit_combo)) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

}  // namespace arc::app

#include "arc/hook.h"
#include "arc/config.h"

#include <windows.h>

namespace {
    HHOOK g_mouseHook = nullptr;
    unsigned int g_modifier_vk = VK_MENU; // default ALT
}

namespace arc {

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        PMSLLHOOKSTRUCT pMouse = reinterpret_cast<PMSLLHOOKSTRUCT>(lParam);
        (void)pMouse; // currently unused; keep for potential future coords usage

        if (wParam == WM_LBUTTONDOWN) {
            if (g_modifier_vk && (GetAsyncKeyState((int)g_modifier_vk) & 0x8000)) {
                INPUT input[2] = {};

                input[0].type = INPUT_MOUSE;
                input[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;

                input[1].type = INPUT_MOUSE;
                input[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;

                SendInput(2, input, sizeof(INPUT));
                return 1; // Block the left click event
            }
        }
    }

    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

bool install_mouse_hook() {
    g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, nullptr, 0);
    return g_mouseHook != nullptr;
}

void remove_mouse_hook() {
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }
}

void apply_hook_config(const arc::Config& cfg) {
    g_modifier_vk = cfg.modifier_vk;
}

} // namespace arc

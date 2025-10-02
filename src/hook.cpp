#include "arc/hook.h"

#include <windows.h>

#include "arc/config.h"

namespace {
HHOOK g_mouseHook = nullptr;
unsigned int g_modifier_vk = VK_MENU;         // default ALT
bool g_ignore_injected = true;
const ULONG_PTR kArcInjectedTag = 0xA17C1C00;  // tag our injected events
}  // namespace

namespace arc {

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        PMSLLHOOKSTRUCT pMouse = reinterpret_cast<PMSLLHOOKSTRUCT>(lParam);
        if (pMouse && pMouse->dwExtraInfo == kArcInjectedTag) {
            // Ignore events we injected ourselves
            return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
        }

        // Ignore or treat cautiously any injected events from other processes or lower IL
        if (g_ignore_injected && pMouse && (pMouse->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED))) {
            return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
        }

        if (wParam == WM_LBUTTONDOWN) {
            if (g_modifier_vk && (GetAsyncKeyState(static_cast<int>(g_modifier_vk)) & 0x8000)) {
                INPUT input[2] = {};

                input[0].type = INPUT_MOUSE;
                input[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
                input[0].mi.dwExtraInfo = kArcInjectedTag;

                input[1].type = INPUT_MOUSE;
                input[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
                input[1].mi.dwExtraInfo = kArcInjectedTag;

                SendInput(2, input, sizeof(INPUT));
                return 1;  // Block the left click event
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

void apply_hook_config(const arc::Config &cfg) {
    g_modifier_vk = cfg.modifier_vk;
    g_ignore_injected = cfg.ignore_injected;
}

}  // namespace arc

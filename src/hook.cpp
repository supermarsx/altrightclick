#include "arc/hook.h"

#include <windows.h>

#include <atomic>
#include <thread>

#include "arc/config.h"
#include "arc/log.h"

namespace {
HHOOK g_mouseHook = nullptr;
unsigned int g_modifier_vk = VK_MENU;         // default ALT
bool g_ignore_injected = true;
const ULONG_PTR kArcInjectedTag = 0xA17C1C00;  // tag our injected events
std::atomic<bool> g_hookRunning{false};
DWORD g_hookThreadId = 0;
std::thread g_hookThread;

// Click/drag discrimination
bool g_tracking = false;
POINT g_startPt{0, 0};
DWORD g_downTick = 0;
unsigned int g_clickTimeMs = 250;  // max duration for a click (ms)
int g_moveRadius = 6;              // max pixels to consider as click

static inline int distance_sq(POINT a, POINT b) {
    int dx = a.x - b.x;
    int dy = a.y - b.y;
    return dx * dx + dy * dy;
}
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
                // Begin tracking: suppress original left-down for potential translation
                g_tracking = true;
                g_startPt = pMouse->pt;
                g_downTick = GetTickCount();
                return 1;  // swallow original left-down
            }
        } else if (wParam == WM_MOUSEMOVE) {
            if (g_tracking) {
                // If moved beyond radius, treat as drag: synthesize left-down and stop tracking
                if (distance_sq(pMouse->pt, g_startPt) > g_moveRadius * g_moveRadius) {
                    INPUT in{};
                    in.type = INPUT_MOUSE;
                    in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    in.mi.dwExtraInfo = kArcInjectedTag;
                    SendInput(1, &in, sizeof(INPUT));
                    g_tracking = false;
                }
            }
        } else if (wParam == WM_LBUTTONUP) {
            if (g_tracking) {
                DWORD dt = GetTickCount() - g_downTick;
                int d2 = distance_sq(pMouse->pt, g_startPt);
                if (dt <= g_clickTimeMs && d2 <= g_moveRadius * g_moveRadius) {
                    // Quick click within radius: translate to right-click
                    INPUT input[2] = {};
                    input[0].type = INPUT_MOUSE;
                    input[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
                    input[0].mi.dwExtraInfo = kArcInjectedTag;
                    input[1].type = INPUT_MOUSE;
                    input[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
                    input[1].mi.dwExtraInfo = kArcInjectedTag;
                    SendInput(2, input, sizeof(INPUT));
                }
                // Swallow the left-up corresponding to our swallowed left-down
                g_tracking = false;
                return 1;
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
    g_clickTimeMs = cfg.click_time_ms;
    g_moveRadius = cfg.move_radius_px;
}

bool start_hook_worker() {
    if (g_hookRunning.load()) return true;
    g_hookRunning.store(true);
    g_hookThread = std::thread([]() {
        g_hookThreadId = GetCurrentThreadId();
        if (!install_mouse_hook()) {
            arc::log_error("Hook worker: failed to install mouse hook");
            g_hookRunning.store(false);
            return;
        }
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        remove_mouse_hook();
        g_hookRunning.store(false);
    });
    return true;
}

void stop_hook_worker() {
    if (g_hookRunning.load()) {
        if (g_hookThreadId) PostThreadMessage(g_hookThreadId, WM_QUIT, 0, 0);
        if (g_hookThread.joinable()) g_hookThread.join();
        g_hookThreadId = 0;
        g_hookRunning.store(false);
    }
}

}  // namespace arc

#include "arc/hook.h"

#include <windows.h>

#include <atomic>
#include <thread>
#include <vector>
#include <future>
#include <utility>

#include "arc/config.h"
#include "arc/log.h"

namespace {
struct HookState {
    std::atomic<HHOOK> mouse_hook{nullptr};
    std::atomic<unsigned int> modifier_vk{VK_MENU};  // default ALT
} g_state;
std::atomic<bool> g_enabled{true};
bool g_ignore_injected = true;
const ULONG_PTR kArcInjectedTag = 0xA17C1C00;  // tag our injected events
std::atomic<bool> g_hookRunning{false};
DWORD g_hookThreadId = 0;
std::thread g_hookThread;
std::vector<unsigned int> g_modifier_combo;  // support modifier combos
arc::Config::Trigger g_trigger = arc::Config::Trigger::Left;

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
        if (!g_enabled.load()) {
            return CallNextHookEx(g_state.mouse_hook.load(), nCode, wParam, lParam);
        }
        PMSLLHOOKSTRUCT pMouse = reinterpret_cast<PMSLLHOOKSTRUCT>(lParam);
        if (pMouse && pMouse->dwExtraInfo == kArcInjectedTag) {
            // Ignore events we injected ourselves
            return CallNextHookEx(g_state.mouse_hook.load(), nCode, wParam, lParam);
        }

        // Ignore or treat cautiously any injected events from other processes or lower IL
        if (g_ignore_injected && pMouse && (pMouse->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED))) {
            return CallNextHookEx(g_state.mouse_hook.load(), nCode, wParam, lParam);
        }

        auto all_mods_down = []() -> bool {
            if (!g_modifier_combo.empty()) {
                for (auto vk : g_modifier_combo) {
                    if (!(GetAsyncKeyState(static_cast<int>(vk)) & 0x8000))
                        return false;
                }
                return true;
            }
            unsigned int mvk = g_state.modifier_vk.load();
            return mvk ? (GetAsyncKeyState(static_cast<int>(mvk)) & 0x8000) != 0 : true;
        };

        auto is_down = [&](WPARAM wp, const MSLLHOOKSTRUCT *m) -> bool {
            (void)m;
            switch (g_trigger) {
            case arc::Config::Trigger::Left:
                return wp == WM_LBUTTONDOWN;
            case arc::Config::Trigger::Middle:
                return wp == WM_MBUTTONDOWN;
            case arc::Config::Trigger::X1:
            case arc::Config::Trigger::X2:
                if (wp == WM_XBUTTONDOWN) {
                    WORD xb = HIWORD(m->mouseData);
                    return (g_trigger == arc::Config::Trigger::X1 && xb == XBUTTON1) ||
                           (g_trigger == arc::Config::Trigger::X2 && xb == XBUTTON2);
                }
                return false;
            }
            return false;
        };
        auto is_up = [&](WPARAM wp, const MSLLHOOKSTRUCT *m) -> bool {
            (void)m;
            switch (g_trigger) {
            case arc::Config::Trigger::Left:
                return wp == WM_LBUTTONUP;
            case arc::Config::Trigger::Middle:
                return wp == WM_MBUTTONUP;
            case arc::Config::Trigger::X1:
            case arc::Config::Trigger::X2:
                if (wp == WM_XBUTTONUP) {
                    WORD xb = HIWORD(m->mouseData);
                    return (g_trigger == arc::Config::Trigger::X1 && xb == XBUTTON1) ||
                           (g_trigger == arc::Config::Trigger::X2 && xb == XBUTTON2);
                }
                return false;
            }
            return false;
        };

        if (is_down(wParam, pMouse)) {
            if (all_mods_down()) {
                // Begin tracking: suppress original left-down for potential translation
                g_tracking = true;
                g_startPt = pMouse->pt;
                g_downTick = GetTickCount();
                return 1;  // swallow original down
            }
        } else if (wParam == WM_MOUSEMOVE) {
            if (g_tracking) {
                // If moved beyond radius, treat as drag: synthesize left-down and stop tracking
                if (distance_sq(pMouse->pt, g_startPt) > g_moveRadius * g_moveRadius) {
                    INPUT in{};
                    in.type = INPUT_MOUSE;
                    // Inject source button down depending on trigger
                    if (g_trigger == arc::Config::Trigger::Left) {
                        in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    } else if (g_trigger == arc::Config::Trigger::Middle) {
                        in.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
                    } else {
                        in.mi.dwFlags = MOUSEEVENTF_XDOWN;
                        in.mi.mouseData = (g_trigger == arc::Config::Trigger::X1) ? XBUTTON1 : XBUTTON2;
                    }
                    in.mi.dwExtraInfo = kArcInjectedTag;
                    SendInput(1, &in, sizeof(INPUT));
                    g_tracking = false;
                }
            }
        } else if (is_up(wParam, pMouse)) {
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

    return CallNextHookEx(g_state.mouse_hook.load(), nCode, wParam, lParam);
}

bool install_mouse_hook() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    HHOOK h = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0);
    g_state.mouse_hook.store(h);
    return h != nullptr;
}

void remove_mouse_hook() {
    HHOOK h = g_state.mouse_hook.exchange(nullptr);
    if (h) {
        UnhookWindowsHookEx(h);
    }
}

void apply_hook_config(const arc::Config &cfg) {
    g_state.modifier_vk.store(cfg.modifier_vk);
    g_modifier_combo = cfg.modifier_combo_vks;
    g_enabled.store(cfg.enabled);
    g_ignore_injected = cfg.ignore_injected;
    g_clickTimeMs = cfg.click_time_ms;
    g_moveRadius = cfg.move_radius_px;
    g_trigger = cfg.trigger;
}

bool start_hook_worker() {
    if (g_hookRunning.load())
        return true;
    g_hookRunning.store(true);
    std::promise<bool> ready;
    auto fut = ready.get_future();
    g_hookThread = std::thread([p = std::move(ready)]() mutable {
        g_hookThreadId = GetCurrentThreadId();
        bool ok = install_mouse_hook();
        p.set_value(ok);
        if (!ok) {
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
    bool ok = fut.get();
    if (!ok) {
        if (g_hookThread.joinable())
            g_hookThread.join();
    }
    return ok;
}

void stop_hook_worker() {
    if (g_hookRunning.load()) {
        if (g_hookThreadId)
            PostThreadMessage(g_hookThreadId, WM_QUIT, 0, 0);
        if (g_hookThread.joinable())
            g_hookThread.join();
        g_hookThreadId = 0;
        g_hookRunning.store(false);
    }
}

}  // namespace arc

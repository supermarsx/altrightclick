/**
 * @file hook.cpp
 * @brief Low-level mouse hook logic and worker thread implementation.
 *
 * Translates a configurable source click (e.g., Alt+Left) into a right-click.
 * The hook runs on a dedicated thread with a private message loop to avoid
 * blocking the main controller/UI thread. A simple click-vs-drag discriminator
 * prevents interfering with drags: short click within a movement radius is
 * translated; long press or movement beyond radius simulates the original
 * source button instead (e.g., left-drag stays a left-drag).
 */

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

/** Private hook state kept in-process. */
struct HookState {
    std::atomic<HHOOK> mouse_hook{nullptr};      ///< Current WH_MOUSE_LL hook handle.
    std::atomic<unsigned int> modifier_vk{VK_MENU};  ///< Legacy single modifier (default ALT).
} g_state;

std::atomic<bool> g_enabled{true};               ///< Master enable flag.
bool g_ignore_injected = true;                   ///< Skip externally injected events.
const ULONG_PTR kArcInjectedTag = 0xA17C1C00;    ///< Tag for events we inject via SendInput.

std::atomic<bool> g_hookRunning{false};          ///< Worker thread running flag.
DWORD g_hookThreadId = 0;                        ///< Hook worker thread id for PostThreadMessage.
std::thread g_hookThread;                        ///< Hook worker thread handle.

std::vector<unsigned int> g_modifier_combo;      ///< Optional combo of modifier VKs.
arc::config::Config::Trigger g_trigger = arc::config::Config::Trigger::Left;  ///< Source button to translate.

// Click/drag discrimination
bool g_tracking = false;                         ///< Tracking a potential click between down/up.
POINT g_startPt{0, 0};                           ///< Mouse position at button down.
DWORD g_downTick = 0;                            ///< Tick count at button down.
unsigned int g_clickTimeMs = 250;                ///< Max duration (ms) to consider a click.
int g_moveRadius = 6;                            ///< Max movement radius (px) to consider a click.

/** Returns squared distance between two points (avoids sqrt). */
static inline int distance_sq(POINT a, POINT b) {
    int dx = a.x - b.x;
    int dy = a.y - b.y;
    return dx * dx + dy * dy;
}
}  // namespace

namespace arc::hook {

/**
 * Low-level mouse hook procedure.
 *
 * Workflow:
 * - If disabled or event is our own injection, pass-through.
 * - If required modifier(s) not held, pass-through.
 * - On trigger-button down: begin tracking; swallow original down.
 * - On move: if moved beyond radius while tracking, synthesize original source-button down and stop tracking
 *   (so drags continue to behave as expected); allow further events to pass-through.
 * - On trigger-button up while tracking: if within time/radius thresholds, inject a right click and swallow the up.
 *
 * Returns 1 to consume events we translate; otherwise delegates to next hook.
 */
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

        // Returns true when all configured modifiers are held down. If no combo
        // is configured, falls back to the legacy single modifier.
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

        // Returns true if wParam represents the configured trigger button down.
        auto is_down = [&](WPARAM wp, const MSLLHOOKSTRUCT *m) -> bool {
            (void)m;
            switch (g_trigger) {
            case arc::config::Config::Trigger::Left:
                return wp == WM_LBUTTONDOWN;
            case arc::config::Config::Trigger::Middle:
                return wp == WM_MBUTTONDOWN;
            case arc::config::Config::Trigger::X1:
            case arc::config::Config::Trigger::X2:
                if (wp == WM_XBUTTONDOWN) {
                    WORD xb = HIWORD(m->mouseData);
                    return (g_trigger == arc::config::Config::Trigger::X1 && xb == XBUTTON1) ||
                           (g_trigger == arc::config::Config::Trigger::X2 && xb == XBUTTON2);
                }
                return false;
            }
            return false;
        };
        // Returns true if wParam represents the configured trigger button up.
        auto is_up = [&](WPARAM wp, const MSLLHOOKSTRUCT *m) -> bool {
            (void)m;
            switch (g_trigger) {
            case arc::config::Config::Trigger::Left:
                return wp == WM_LBUTTONUP;
            case arc::config::Config::Trigger::Middle:
                return wp == WM_MBUTTONUP;
            case arc::config::Config::Trigger::X1:
            case arc::config::Config::Trigger::X2:
                if (wp == WM_XBUTTONUP) {
                    WORD xb = HIWORD(m->mouseData);
                    return (g_trigger == arc::config::Config::Trigger::X1 && xb == XBUTTON1) ||
                           (g_trigger == arc::config::Config::Trigger::X2 && xb == XBUTTON2);
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
                    if (g_trigger == arc::config::Config::Trigger::Left) {
                        in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    } else if (g_trigger == arc::config::Config::Trigger::Middle) {
                        in.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
                    } else {
                        in.mi.dwFlags = MOUSEEVENTF_XDOWN;
                        in.mi.mouseData = (g_trigger == arc::config::Config::Trigger::X1) ? XBUTTON1 : XBUTTON2;
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

/**
 * Installs the WH_MOUSE_LL hook for the current process.
 * Should be called on the hook worker thread.
 */
bool install() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    HHOOK h = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0);
    g_state.mouse_hook.store(h);
    return h != nullptr;
}

/**
 * Uninstalls the WH_MOUSE_LL hook if previously installed.
 */
void remove() {
    HHOOK h = g_state.mouse_hook.exchange(nullptr);
    if (h) {
        UnhookWindowsHookEx(h);
    }
}

/**
 * Applies runtime configuration to the hook state.
 */
void apply_hook_config(const arc::config::Config &cfg) {
    g_state.modifier_vk.store(cfg.modifier_vk);
    g_modifier_combo = cfg.modifier_combo_vks;
    g_enabled.store(cfg.enabled);
    g_ignore_injected = cfg.ignore_injected;
    g_clickTimeMs = cfg.click_time_ms;
    g_moveRadius = cfg.move_radius_px;
    g_trigger = cfg.trigger;
}

/**
 * Starts the hook worker thread and installs the hook.
 * The worker pumps a private message loop until @ref stop.
 *
 * @return true if the hook was installed successfully.
 */
bool start() {
    if (g_hookRunning.load())
        return true;
    g_hookRunning.store(true);
    std::promise<bool> ready;
    auto fut = ready.get_future();
    g_hookThread = std::thread([p = std::move(ready)]() mutable {
        g_hookThreadId = GetCurrentThreadId();
        bool ok = install();
        p.set_value(ok);
        if (!ok) {
            arc::log::error("Hook worker: failed to install mouse hook");
            g_hookRunning.store(false);
            return;
        }
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        remove();
        g_hookRunning.store(false);
    });
    bool ok = fut.get();
    if (!ok) {
        if (g_hookThread.joinable())
            g_hookThread.join();
    }
    return ok;
}

/**
 * Requests the hook worker to quit and waits for it to join.
 */
void stop() {
    if (g_hookRunning.load()) {
        if (g_hookThreadId)
            PostThreadMessage(g_hookThreadId, WM_QUIT, 0, 0);
        if (g_hookThread.joinable())
            g_hookThread.join();
        g_hookThreadId = 0;
        g_hookRunning.store(false);
    }
}

}  // namespace arc::hook

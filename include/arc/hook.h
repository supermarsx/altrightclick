#pragma once

#include <windows.h>

namespace arc {
struct Config;
}

namespace arc {

// Installs the low-level mouse hook. Returns true on success.
bool install_mouse_hook();

// Removes the mouse hook if installed.
void remove_mouse_hook();

// Low-level mouse hook procedure.
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

// Configure hook behavior (e.g., modifier key) using loaded config.
void apply_hook_config(const arc::Config& cfg);

}  // namespace arc

#pragma once

#include <windows.h>
#include <string>

namespace arc {

struct Config;  // fwd decl

struct TrayContext {
    // Live pointers; owner must outlive the tray window
    Config* cfg;
    std::string* config_path;
};

// Initializes a system tray icon. Returns a hidden window handle that owns the tray icon.
// The window will handle basic messages and allow graceful shutdown via tray menu (Exit).
HWND tray_init(HINSTANCE hInstance, const std::wstring &tooltip, TrayContext* ctx);

// Removes the tray icon and destroys the hidden window.
void tray_cleanup(HWND hwnd);

}  // namespace arc

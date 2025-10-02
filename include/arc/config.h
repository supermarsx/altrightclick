#pragma once

#include <string>

namespace arc {

struct Config {
    bool enabled = true;
    bool show_tray = true;
    // Modifier key: VK_MENU (Alt), VK_CONTROL, VK_SHIFT, VK_LWIN
    unsigned int modifier_vk = 0x12;  // VK_MENU
    // Exit key to stop the app when not running as service
    unsigned int exit_vk = 0x1B;  // VK_ESCAPE
    // Ignore externally injected mouse events (LLMHF_INJECTED/LLMHF_LOWER_IL_INJECTED)
    bool ignore_injected = true;
    // Click vs drag discrimination (defaults)
    // Maximum press duration (ms) to treat as a click
    unsigned int click_time_ms = 250;
    // Maximum movement radius (pixels) to treat as a click
    int move_radius_px = 6;
    // Logging
    std::string log_level = "info";  // error|warn|info|debug
    std::string log_file;             // empty for stdout/stderr only
};

// Loads configuration from file path. If not found, keeps defaults.
// Supports simple key=value lines (case-insensitive keys):
// enabled=true|false, show_tray=true|false, modifier=ALT|CTRL|SHIFT|WIN, exit_key=ESC|F12
// ignore_injected=true|false, click_time_ms=<uint>, move_radius_px=<int>
// log_level=error|warn|info|debug, log_file=<path>
Config load_config(const std::string &path);

// Finds a default config path: <exe_dir>\\config.ini if present, otherwise
// %APPDATA%\\altrightclick\\config.ini
std::string default_config_path();

// Save configuration back to the given path. Returns true on success.
bool save_config(const std::string &path, const Config &cfg);

}  // namespace arc

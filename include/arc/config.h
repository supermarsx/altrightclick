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
};

// Loads configuration from file path. If not found, keeps defaults.
// Supports simple key=value lines (case-insensitive keys):
// enabled=true|false, show_tray=true|false, modifier=ALT|CTRL|SHIFT|WIN, exit_key=ESC|F12
Config load_config(const std::string &path);

// Finds a default config path: <exe_dir>\\config.ini if present, otherwise
// %APPDATA%\\altrightclick\\config.ini
std::string default_config_path();

}  // namespace arc

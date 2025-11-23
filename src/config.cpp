/**
 * @file config.cpp
 * @brief Configuration parsing and persistence helpers.
 *
 * This translation unit provides functionality to load and save the application's
 * configuration from a simple key=value text file (UTF-8 encoded paths are used
 * with std::filesystem::path). The parser is tolerant of comments and empty
 * lines and performs case-insensitive key matching. Several small helpers are
 * implemented locally (string trimming/case conversion and simple key-to-virtual-
 * key mapping).
 *
 * The configuration format is intentionally simple and human-editable. Missing
 * or invalid values are ignored and sensible defaults from the Config struct
 * are preserved.
 */

#include "arc/config.h"

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <string>
#include <vector>

#include "arc/log.h"

namespace arc::config {

/**
 * @brief Convert a string to lowercase (ASCII / C locale aware transformation).
 *
 * This helper performs an in-place lowercase conversion and returns the
 * converted copy. It is primarily intended for case-insensitive key and value
 * comparison within the parser.
 *
 * @param s Input string copy to convert (passed by value intentionally).
 * @return std::string Lowercased string.
 */
static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/**
 * @brief Trim whitespace from both ends of a string.
 *
 * Removes common ASCII whitespace characters (space, tab, CR, LF) from the
 * beginning and end of the input string. If the string contains only
 * whitespace an empty string is returned.
 *
 * @param s Input string to trim.
 * @return std::string Trimmed substring (may be empty).
 */
static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    return s.substr(a, b - a + 1);
}

/**
 * @brief Map a textual key name to a Win32 virtual-key code.
 *
 * Recognizes a small set of common names used in the configuration file such
 * as "ALT", "CTRL", "SHIFT", "WIN", "ESC", and "F12". Matching is
 * case-insensitive. Unknown names return 0 which callers treat as an ignored
 * or missing mapping.
 *
 * @param name Name of the key (case-insensitive).
 * @return unsigned int Virtual-key code on success, or 0 if the name is unknown.
 */
static unsigned int vk_from_str(const std::string &name) {
    std::string n = to_lower(trim(name));
    if (n.empty())
        return 0;
    if (n == "alt")
        return VK_MENU;
    if (n == "ctrl" || n == "control")
        return VK_CONTROL;
    if (n == "shift")
        return VK_SHIFT;
    if (n == "win" || n == "lwin" || n == "super")
        return VK_LWIN;
    if (n == "esc" || n == "escape")
        return VK_ESCAPE;
    if (n == "f12")
        return VK_F12;
    if (n.size() == 1) {
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(n[0])));
        if (c >= 'A' && c <= 'Z')
            return static_cast<unsigned int>(c);
        if (c >= '0' && c <= '9')
            return static_cast<unsigned int>(c);
    }
    if (n[0] == 'f' && n.size() <= 3) {
        try {
            int idx = std::stoi(n.substr(1));
            if (idx >= 1 && idx <= 24)
                return VK_F1 + static_cast<unsigned int>(idx - 1);
        } catch (...) {
        }
    }
    if (n == "space")
        return VK_SPACE;
    if (n == "tab")
        return VK_TAB;
    if (n == "enter" || n == "return")
        return VK_RETURN;
    if (n == "backspace" || n == "bksp")
        return VK_BACK;
    if (n == "delete" || n == "del")
        return VK_DELETE;
    if (n == "insert" || n == "ins")
        return VK_INSERT;
    if (n == "home")
        return VK_HOME;
    if (n == "end")
        return VK_END;
    if (n == "pgup" || n == "pageup")
        return VK_PRIOR;
    if (n == "pgdn" || n == "pagedown")
        return VK_NEXT;
    if (n == "up")
        return VK_UP;
    if (n == "down")
        return VK_DOWN;
    if (n == "left")
        return VK_LEFT;
    if (n == "right")
        return VK_RIGHT;
    if (n == "caps")
        return VK_CAPITAL;
    if (n == "pause" || n == "break")
        return VK_PAUSE;
    return 0;  // unknown -> ignored by caller
}

static std::string vk_to_string(unsigned int vk) {
    switch (vk) {
    case VK_MENU:
        return "ALT";
    case VK_CONTROL:
        return "CTRL";
    case VK_SHIFT:
        return "SHIFT";
    case VK_LWIN:
        return "WIN";
    case VK_ESCAPE:
        return "ESC";
    case VK_TAB:
        return "TAB";
    case VK_SPACE:
        return "SPACE";
    case VK_RETURN:
        return "ENTER";
    case VK_BACK:
        return "BACKSPACE";
    case VK_DELETE:
        return "DELETE";
    case VK_INSERT:
        return "INSERT";
    case VK_HOME:
        return "HOME";
    case VK_END:
        return "END";
    case VK_PRIOR:
        return "PGUP";
    case VK_NEXT:
        return "PGDN";
    case VK_UP:
        return "UP";
    case VK_DOWN:
        return "DOWN";
    case VK_LEFT:
        return "LEFT";
    case VK_RIGHT:
        return "RIGHT";
    case VK_PAUSE:
        return "PAUSE";
    case VK_CAPITAL:
        return "CAPS";
    default:
        break;
    }
    if (vk >= 'A' && vk <= 'Z')
        return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9')
        return std::string(1, static_cast<char>(vk));
    if (vk >= VK_F1 && vk <= VK_F24)
        return "F" + std::to_string(vk - VK_F1 + 1);
    return "";
}

/**
 * @brief Parse a modifier combo string into a vector of virtual-key codes.
 *
 * The configuration accepts modifier specifications such as "ALT+CTRL" or
 * "ALT,CTRL". This helper splits the input on both '+' and ',' delimiters and
 * converts each token to a virtual-key code using vk_from_str(). The output
 * vector is cleared before populating.
 *
 * @param val Input modifier string from the config file.
 * @param out Pointer to a vector<unsigned int> to receive the parsed VK codes.
 */
static void parse_vk_combo(const std::string &val, std::vector<unsigned int> *out) {
    out->clear();
    std::string tmp;
    for (size_t i = 0; i <= val.size(); ++i) {
        char c = (i < val.size()) ? val[i] : ',';  // treat end as delimiter
        if (c == '+' || c == ',') {
            std::string tok = trim(tmp);
            if (!tok.empty()) {
                unsigned int vk = vk_from_str(tok);
                if (vk)
                    out->push_back(vk);
            }
            tmp.clear();
        } else {
            tmp.push_back(c);
        }
    }
}

/**
 * @brief Convert a textual trigger name into the Config::Trigger enum.
 *
 * Supports names such as "middle", "m", "mbutton", "x1", "xbutton1",
 * "x2", "xbutton2" and defaults to LEFT when the name is not recognized.
 * Matching is case-insensitive.
 *
 * @param name Trigger name from configuration.
 * @return Config::Trigger Corresponding trigger enum value.
 */
static Config::Trigger trigger_from_str(const std::string &name) {
    std::string n = to_lower(name);
    if (n == "middle" || n == "m" || n == "mbutton")
        return Config::Trigger::Middle;
    if (n == "x1" || n == "xbutton1")
        return Config::Trigger::X1;
    if (n == "x2" || n == "xbutton2")
        return Config::Trigger::X2;
    return Config::Trigger::Left;
}

static std::string combo_to_string(const std::vector<unsigned int> &combo) {
    std::string out;
    for (auto vk : combo) {
        std::string name = vk_to_string(vk);
        if (name.empty())
            continue;
        if (!out.empty())
            out += "+";
        out += name;
    }
    return out;
}

/**
 * @brief Load configuration from a file path.
 *
 * The parser reads the file line-by-line, supports comment lines starting with
 * '#' or ';', trims whitespace, and parses lines of the form "key=value".
 * Keys are matched case-insensitively and unknown keys are ignored. Invalid
 * values leave the corresponding Config fields at their defaults.
 *
 * @param path Filesystem path to the configuration file (UTF-8 capable via std::filesystem).
 * @return Config Parsed configuration object. If the file cannot be opened the
 *                default-constructed Config is returned.
 */
Config load(const std::filesystem::path &path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        return cfg;
    }
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = to_lower(trim(line.substr(0, pos)));
        std::string val = trim(line.substr(pos + 1));
        std::string vall = to_lower(val);
        if (key == "enabled") {
            cfg.enabled = (vall == "1" || vall == "true" || vall == "yes");
        } else if (key == "show_tray") {
            cfg.show_tray = (vall == "1" || vall == "true" || vall == "yes");
        } else if (key == "modifier") {
            // Allow combos: ALT+CTRL or ALT,CTRL; also back-compat single key
            std::vector<unsigned int> mods;
            parse_vk_combo(val, &mods);
            if (!mods.empty()) {
                cfg.modifier_combo_vks = mods;
                cfg.modifier_vk = mods.front();
            } else {
                unsigned int vk = vk_from_str(val);
                if (vk)
                    cfg.modifier_vk = vk;
            }
        } else if (key == "trigger") {
            cfg.trigger = trigger_from_str(val);
        } else if (key == "exit_key") {
            if (vall == "disabled" || vall == "none" || vall == "false" || vall == "0" || vall == "off") {
                cfg.exit_hotkey_enabled = false;
                cfg.exit_hotkey_combo_vks.clear();
                cfg.exit_vk = 0;
            } else {
                std::vector<unsigned int> combo;
                parse_vk_combo(val, &combo);
                if (!combo.empty()) {
                    cfg.exit_hotkey_enabled = true;
                    cfg.exit_hotkey_combo_vks = combo;
                    cfg.exit_vk = combo.front();
                } else {
                    unsigned int vk = vk_from_str(val);
                    if (vk) {
                        cfg.exit_hotkey_enabled = true;
                        cfg.exit_hotkey_combo_vks = {vk};
                        cfg.exit_vk = vk;
                    }
                }
            }
        } else if (key == "ignore_injected") {
            cfg.ignore_injected = (vall == "1" || vall == "true" || vall == "yes");
        } else if (key == "disable_right_click") {
            cfg.disable_right_click = (vall == "1" || vall == "true" || vall == "yes");
        } else if (key == "click_time_ms") {
            try {
                unsigned int v = static_cast<unsigned int>(std::stoul(vall));
                if (v > 0 && v < 5000)
                    cfg.click_time_ms = v;
            } catch (...) {
            }
        } else if (key == "move_radius_px") {
            try {
                int v = std::stoi(vall);
                if (v >= 0 && v < 100)
                    cfg.move_radius_px = v;
            } catch (...) {
            }
        } else if (key == "log_level") {
            cfg.log_level = vall;
        } else if (key == "log_file") {
            cfg.log_file = val;  // keep original as path
        } else if (key == "log_thread_id") {
            cfg.log_thread_id = (vall == "1" || vall == "true" || vall == "yes");
        } else if (key == "watch_config") {
            cfg.watch_config = (vall == "1" || vall == "true" || vall == "yes");
        } else if (key == "persistence" || key == "persistence_enabled") {
            cfg.persistence_enabled = (vall == "1" || vall == "true" || vall == "yes");
        } else if (key == "persistence_max_restarts") {
            try {
                cfg.persistence_max_restarts = std::max(0, std::stoi(vall));
            } catch (...) {
            }
        } else if (key == "persistence_window_sec") {
            try {
                cfg.persistence_window_sec = std::max(1, std::stoi(vall));
            } catch (...) {
            }
        } else if (key == "persistence_backoff_ms") {
            try {
                cfg.persistence_backoff_ms = std::max(0, std::stoi(vall));
            } catch (...) {
            }
        } else if (key == "persistence_backoff_max_ms") {
            try {
                cfg.persistence_backoff_max_ms = std::max(0, std::stoi(vall));
            } catch (...) {
            }
        } else if (key == "persistence_stop_timeout_ms") {
            try {
                cfg.persistence_stop_timeout_ms = std::max(0, std::stoi(vall));
            } catch (...) {
            }
        }
    }
    return cfg;
}

/**
 * @brief Get directory of the running executable.
 *
 * This helper returns the directory containing the current executable by
 * calling GetModuleFileNameW(nullptr,...). The returned path is converted to
 * a std::filesystem::path. On failure a path containing "." is returned.
 *
 * @return std::filesystem::path Directory of the current executable.
 */
static std::filesystem::path get_exe_dir() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring wpath(buf, (len > 0 ? len : 0));
    size_t pos = wpath.find_last_of(L"\\/");
    std::wstring wdir = (pos == std::wstring::npos) ? L"." : wpath.substr(0, pos);
    return std::filesystem::path(wdir);
}

/**
 * @brief Compute the default configuration file path.
 *
 * Preference order:
 *  1. If <exe_dir>\config.ini exists, return that path.
 *  2. Otherwise return %APPDATA%\altrightclick\config.ini if the known folder
 *     API succeeds.
 *  3. Fallback to <exe_dir>\config.ini.
 *
 * @return std::filesystem::path Default configuration file path.
 */
std::filesystem::path default_path() {
    // Prefer exe_dir\config.ini
    std::filesystem::path local = get_exe_dir() / "config.ini";
    std::ifstream f(local);
    if (f.good())
        return local;

    // Fallback to %APPDATA%\altrightclick\config.ini
    PWSTR appdataW = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdataW))) {
        std::wstring wpath = std::wstring(appdataW) + L"\\altrightclick\\config.ini";
        CoTaskMemFree(appdataW);
        return std::filesystem::path(wpath);
    } else {
        arc::log::warn("SHGetKnownFolderPath failed; using local config path");
    }
    return local;  // fallback
}

/**
 * @brief Write configuration to disk using a simple text format.
 *
 * The function creates the parent directory if necessary and writes a human-
 * readable key=value file including comments describing accepted values. The
 * file will be truncated or created as needed.
 *
 * @param path Destination filesystem path where the config will be written.
 * @param cfg  Config object containing the settings to persist.
 * @return true if the file was written successfully and the output stream is in a good state.
 */
bool save(const std::filesystem::path &path, const Config &cfg) {
    // Ensure parent directory exists
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << "# altrightclick config\n";
    out << "# Enable/disable the app (true/false)\n";
    out << "enabled=" << (cfg.enabled ? "true" : "false") << "\n\n";
    out << "# Show tray icon with runtime settings (true/false)\n";
    out << "show_tray=" << (cfg.show_tray ? "true" : "false") << "\n\n";
    // Map modifier back to string/combo
    std::string mod = combo_to_string(cfg.modifier_combo_vks);
    if (mod.empty()) {
        mod = vk_to_string(cfg.modifier_vk);
    }
    if (mod.empty())
        mod = "ALT";
    out << "# Modifier key for translating left-click to right-click (ALT|CTRL|SHIFT|WIN)\n";
    out << "# Multiple modifiers allowed; e.g., ALT+CTRL or ALT,CTRL\n";
    out << "modifier=" << mod << "\n\n";
    // Exit hotkey
    out << "# Exit hotkey to stop the app (e.g., ESC, CTRL+ALT+Q). Set to DISABLED to turn off.\n";
    if (!cfg.exit_hotkey_enabled || cfg.exit_hotkey_combo_vks.empty()) {
        out << "exit_key=DISABLED\n\n";
    } else {
        std::string exitk = combo_to_string(cfg.exit_hotkey_combo_vks);
        if (exitk.empty())
            exitk = vk_to_string(cfg.exit_vk);
        if (exitk.empty())
            exitk = "ESC";
        out << "exit_key=" << exitk << "\n\n";
    }
    out << "# Ignore externally injected events (true/false)\n";
    out << "ignore_injected=" << (cfg.ignore_injected ? "true" : "false") << "\n\n";
    out << "# Treat physical right-clicks as left-clicks (true/false)\n";
    out << "disable_right_click=" << (cfg.disable_right_click ? "true" : "false") << "\n\n";
    out << "# Max press duration in milliseconds to translate as a click (10-5000)\n";
    out << "click_time_ms=" << cfg.click_time_ms << "\n\n";
    out << "# Max pointer movement radius in pixels to still translate as click (0-100)\n";
    out << "move_radius_px=" << cfg.move_radius_px << "\n\n";
    out << "# Source button to translate (LEFT|MIDDLE|X1|X2)\n";
    const char *trig = "LEFT";
    if (cfg.trigger == Config::Trigger::Middle)
        trig = "MIDDLE";
    else if (cfg.trigger == Config::Trigger::X1)
        trig = "X1";
    else if (cfg.trigger == Config::Trigger::X2)
        trig = "X2";
    out << "trigger=" << trig << "\n\n";
    out << "# Logging level: error|warn|info|debug\n";
    out << "log_level=" << cfg.log_level << "\n";
    if (!cfg.log_file.empty()) {
        out << "# Log file path (optional)\n";
        out << "log_file=" << cfg.log_file << "\n";
    }
    out << "# Include thread id in each log line (true/false)\n";
    out << "log_thread_id=" << (cfg.log_thread_id ? "true" : "false") << "\n";
    out << "\n# Live reload the config file on changes (true/false)\n";
    out << "watch_config=" << (cfg.watch_config ? "true" : "false") << "\n";
    out << "\n# Restart the app if it crashes (true/false). Applies only to interactive mode.\n";
    out << "persistence=" << (cfg.persistence_enabled ? "true" : "false") << "\n";
    out << "# Persistence tuning (effective when persistence=true)\n";
    out << "persistence_max_restarts=" << cfg.persistence_max_restarts << "\n";
    out << "persistence_window_sec=" << cfg.persistence_window_sec << "\n";
    out << "persistence_backoff_ms=" << cfg.persistence_backoff_ms << "\n";
    out << "persistence_backoff_max_ms=" << cfg.persistence_backoff_max_ms << "\n";
    out << "persistence_stop_timeout_ms=" << cfg.persistence_stop_timeout_ms << "\n";
    out.flush();
    return out.good();
}

}  // namespace arc::config

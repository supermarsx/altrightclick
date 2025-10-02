#include "arc/config.h"

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include "arc/log.h"

namespace arc {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    return s.substr(a, b - a + 1);
}

static unsigned int vk_from_str(const std::string &name) {
    std::string n = to_lower(name);
    if (n == "alt")
        return VK_MENU;
    if (n == "ctrl" || n == "control")
        return VK_CONTROL;
    if (n == "shift")
        return VK_SHIFT;
    if (n == "win" || n == "lwin")
        return VK_LWIN;
    if (n == "esc" || n == "escape")
        return VK_ESCAPE;
    if (n == "f12")
        return VK_F12;
    return 0;  // unknown -> ignored by caller
}

Config load_config(const std::string &path) {
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
            unsigned int vk = vk_from_str(val);
            if (vk)
                cfg.modifier_vk = vk;
        } else if (key == "exit_key") {
            unsigned int vk = vk_from_str(val);
            if (vk)
                cfg.exit_vk = vk;
        } else if (key == "ignore_injected") {
            cfg.ignore_injected = (vall == "1" || vall == "true" || vall == "yes");
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
        }
    }
    return cfg;
}

static std::string get_exe_dir() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, (len > 0 ? len : 0));
    size_t pos = path.find_last_of(L"\\/");
    std::wstring dir = (pos == std::wstring::npos) ? L"." : path.substr(0, pos);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? bytes - 1 : 0, '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

std::string default_config_path() {
    // Prefer exe_dir\\config.ini
    std::string exe_dir = get_exe_dir();
    std::string local = exe_dir + "\\config.ini";
    std::ifstream f(local);
    if (f.good())
        return local;

    // Fallback to %APPDATA%\\altrightclick\\config.ini
    PWSTR appdataW = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdataW))) {
        std::wstring wpath = std::wstring(appdataW) + L"\\altrightclick\\config.ini";
        CoTaskMemFree(appdataW);
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string out(bytes > 0 ? bytes - 1 : 0, '\0');
        if (bytes > 0)
            WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, out.data(), bytes, nullptr, nullptr);
        return out;
    } else {
        arc::log_warn("SHGetKnownFolderPath failed; using local config path");
    }
    return local;  // fallback
}

bool save_config(const std::string &path, const Config &cfg) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << "# altrightclick config\n";
    out << "enabled=" << (cfg.enabled ? "true" : "false") << "\n";
    out << "show_tray=" << (cfg.show_tray ? "true" : "false") << "\n";
    // Map modifier back to string
    std::string mod = "ALT";
    if (cfg.modifier_vk == VK_CONTROL) mod = "CTRL";
    else if (cfg.modifier_vk == VK_SHIFT) mod = "SHIFT";
    else if (cfg.modifier_vk == VK_LWIN) mod = "WIN";
    out << "modifier=" << mod << "\n";
    // Exit key
    std::string exitk = (cfg.exit_vk == VK_F12) ? "F12" : "ESC";
    out << "exit_key=" << exitk << "\n";
    out << "ignore_injected=" << (cfg.ignore_injected ? "true" : "false") << "\n";
    out << "click_time_ms=" << cfg.click_time_ms << "\n";
    out << "move_radius_px=" << cfg.move_radius_px << "\n";
    out << "log_level=" << cfg.log_level << "\n";
    if (!cfg.log_file.empty()) out << "log_file=" << cfg.log_file << "\n";
    out.flush();
    return out.good();
}

}  // namespace arc

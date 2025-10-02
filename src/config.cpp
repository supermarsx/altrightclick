#include "arc/config.h"

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace arc {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

static unsigned int vk_from_str(const std::string& name) {
    std::string n = to_lower(name);
    if (n == "alt") return VK_MENU;
    if (n == "ctrl" || n == "control") return VK_CONTROL;
    if (n == "shift") return VK_SHIFT;
    if (n == "win" || n == "lwin") return VK_LWIN;
    if (n == "esc" || n == "escape") return VK_ESCAPE;
    if (n == "f12") return VK_F12;
    return 0; // unknown -> ignored by caller
}

Config load_config(const std::string& path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        return cfg;
    }
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = to_lower(trim(line.substr(0, pos)));
        std::string val = trim(line.substr(pos + 1));
        std::string vall = to_lower(val);
        if (key == "enabled") {
            cfg.enabled = (vall == "1" || vall == "true" || vall == "yes");
        } else if (key == "show_tray") {
            cfg.show_tray = (vall == "1" || vall == "true" || vall == "yes");
        } else if (key == "modifier") {
            unsigned int vk = vk_from_str(val);
            if (vk) cfg.modifier_vk = vk;
        } else if (key == "exit_key") {
            unsigned int vk = vk_from_str(val);
            if (vk) cfg.exit_vk = vk;
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
    if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

std::string default_config_path() {
    // Prefer exe_dir\\config.ini
    std::string exe_dir = get_exe_dir();
    std::string local = exe_dir + "\\config.ini";
    std::ifstream f(local);
    if (f.good()) return local;

    // Fallback to %APPDATA%\\altrightclick\\config.ini
    PWSTR appdataW = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdataW))) {
        std::wstring wpath = std::wstring(appdataW) + L"\\altrightclick\\config.ini";
        CoTaskMemFree(appdataW);
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string out(bytes > 0 ? bytes - 1 : 0, '\0');
        if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, out.data(), bytes, nullptr, nullptr);
        return out;
    }
    return local; // fallback
}

} // namespace arc

// Main entry: tray app with optional service management via CLI.

#include <windows.h>

#include <iostream>
#include <string>
#include <vector>

#include "arc/hook.h"
#include "arc/app.h"
#include "arc/tray.h"
#include "arc/config.h"
#include "arc/service.h"
#include "arc/singleton.h"

static std::wstring to_w(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static std::wstring get_module_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf);
}

static void print_help() {
    std::cout << "Usage: altrightclick [options]\n"
                 "\nOptions:\n"
                 "  --config <path>        Use explicit config file path\n"
                 "  --install              Install Windows service\n"
                 "  --uninstall            Uninstall Windows service\n"
                 "  --start                Start Windows service\n"
                 "  --stop                 Stop Windows service\n"
                 "  --service              Run as service (internal)\n"
                 "  --help                 Show this help\n";
}

int main(int argc, char** argv) {
    std::string config_path = arc::default_config_path();

    // Parse args
    bool do_install = false, do_uninstall = false, do_start = false, do_stop = false, run_as_service = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (a == "--install") {
            do_install = true;
        } else if (a == "--uninstall") {
            do_uninstall = true;
        } else if (a == "--start") {
            do_start = true;
        } else if (a == "--stop") {
            do_stop = true;
        } else if (a == "--service") {
            run_as_service = true;
        } else if (a == "--help" || a == "-h" || a == "-?") {
            print_help();
            return 0;
        }
    }

    const std::wstring svcName = L"AltRightClickService";
    if (do_install || do_uninstall || do_start || do_stop) {
        std::wstring exe = get_module_path();
        std::wstring cmd = L"\"" + exe + L"\" --service";
        if (!config_path.empty()) {
            cmd += L" --config \"" + to_w(config_path) + L"\"";
        }
        bool ok = true;
        if (do_install) ok = ok && arc::service_install(svcName, L"Alt Right Click", cmd);
        if (do_uninstall) ok = ok && arc::service_uninstall(svcName);
        if (do_start) ok = ok && arc::service_start(svcName);
        if (do_stop) ok = ok && arc::service_stop(svcName);
        std::cout << (ok ? "OK" : "FAILED") << std::endl;
        return ok ? 0 : 1;
    }

    if (run_as_service) {
        return arc::service_run(svcName);
    }

    // Normal interactive app: enforce single instance, load config, init hook, tray, message loop
    arc::SingletonGuard instance(arc::default_singleton_name());
    if (!instance.acquired()) {
        std::cerr << "altrightclick is already running." << std::endl;
        return 0;
    }
    arc::Config cfg = arc::load_config(config_path);
    arc::apply_hook_config(cfg);

    if (!cfg.enabled) {
        std::cerr << "altrightclick: disabled in config." << std::endl;
        return 0;
    }

    if (!arc::install_mouse_hook()) {
        std::cerr << "Failed to install mouse hook!" << std::endl;
        return 1;
    }

    std::wcout << L"Alt + Left Click => Right Click. Press key to exit." << std::endl;

    HWND hwndTray = nullptr;
    if (cfg.show_tray) {
        hwndTray = arc::tray_init(GetModuleHandleW(nullptr), L"AltRightClick running (Alt+Left => Right)");
    }

    arc::run_message_loop(cfg.exit_vk);

    if (hwndTray) {
        arc::tray_cleanup(hwndTray);
    }
    arc::remove_mouse_hook();
    return 0;
}

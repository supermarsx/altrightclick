/**
 * @file main.cpp
 * @brief Main controller for interactive app and CLI/service management.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <filesystem>

#include "arc/hook.h"
#include "arc/tray.h"
#include "arc/config.h"
#include "arc/service.h"
#include "arc/singleton.h"
#include "arc/task.h"
#include "arc/log.h"
#include "altrightclick/version.h"

/** Converts a UTF-8 string to UTF-16 (Windows wide). */
static std::wstring to_w(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

/** Returns the full path of the current executable. */
static std::wstring get_module_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf);
}

// Global shutdown flag toggled by console control events (Ctrl+C, close, etc.).
static std::atomic<bool> g_console_shutdown{false};

// Console control handler to request graceful shutdown.
static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_console_shutdown.store(true);
        return TRUE;  // handled
    default:
        break;
    }
    return FALSE;
}

/** Returns true if a wide-path file exists. */
static bool file_exists_w(const std::wstring &path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

/** Conservative validation for embedding a UTF-8 arg in a quoted command. */
static bool is_safe_arg_utf8(const std::string &s) {
    for (unsigned char c : s) {
        if (c < 0x20)
            return false;  // control chars (CR, LF, TAB, etc.)
        if (c == '"')
            return false;  // disallow quotes to avoid breaking quoting
    }
    return true;
}

/** Returns true if the current process has elevated (admin) token. */
static bool is_elevated() {
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elev{};
        DWORD ret = 0;
        BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &ret);
        CloseHandle(token);
        if (ok)
            return elev.TokenIsElevated != 0;
    }
    // Fallback: check membership in Administrators
    BOOL isMember = FALSE;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
                                 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isMember);
        FreeSid(adminGroup);
    }
    return isMember == TRUE;
}

/** Prints CLI usage help to stdout. */
static void print_help() {
    std::cout << "Usage: altrightclick [options]\n"
                 "\nOptions:\n"
                 "  --config <path>        Use explicit config file path\n"
                 "  --generate-config      Write a default config (and exit)\n"
                 "  --log-level <lvl>      Set logging level (error|warn|info|debug)\n"
                 "  --log-file <path>      Append logs to file\n"
                 "  --install              Install Windows service\n"
                 "  --uninstall            Uninstall Windows service\n"
                 "  --start                Start Windows service\n"
                 "  --stop                 Stop Windows service\n"
                 "  --service-status       Check if service is running\n"
                 "  --service              Run as service (internal)\n"
                 "  --task-install         Install Scheduled Task (on logon, highest privs)\n"
                 "  --task-uninstall       Uninstall Scheduled Task\n"
                 "  --task-update          Update Scheduled Task target/args\n"
                 "  --task-status          Check if Scheduled Task exists\n"
                 "  --help                 Show this help\n";
}

/** Program entry point. Parses CLI, starts workers, and coordinates shutdown. */
int main(int argc, char **argv) {
    std::string config_path = arc::config::default_path().u8string();

    // Parse args
    bool do_install = false, do_uninstall = false, do_start = false, do_stop = false, do_service_status = false,
         run_as_service = false;
    bool do_task_install = false, do_task_uninstall = false, do_task_update = false, do_task_status = false;
    std::string cli_log_level;
    std::string cli_log_file;
    bool do_generate_config = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (a == "--generate-config") {
            do_generate_config = true;
        } else if (a == "--log-level" && i + 1 < argc) {
            cli_log_level = argv[++i];
        } else if (a == "--log-file" && i + 1 < argc) {
            cli_log_file = argv[++i];
        } else if (a == "--install") {
            do_install = true;
        } else if (a == "--uninstall") {
            do_uninstall = true;
        } else if (a == "--start") {
            do_start = true;
        } else if (a == "--stop") {
            do_stop = true;
        } else if (a == "--service-status") {
            do_service_status = true;
        } else if (a == "--service") {
            run_as_service = true;
        } else if (a == "--task-install") {
            do_task_install = true;
        } else if (a == "--task-uninstall") {
            do_task_uninstall = true;
        } else if (a == "--task-update") {
            do_task_update = true;
        } else if (a == "--task-status") {
            do_task_status = true;
        } else if (a == "--help" || a == "-h" || a == "-?") {
            print_help();
            return 0;
        }
    }

    const std::wstring svcName = L"AltRightClickService";

    // Generate config and exit
    if (do_generate_config) {
        arc::config::Config defaults;
        if (arc::config::save(config_path, defaults)) {
            std::cout << "Wrote default config to " << config_path << std::endl;
            return 0;
        }
        std::cerr << "Failed to write config to " << config_path << std::endl;
        return 1;
    }
    if (do_install || do_uninstall || do_start || do_stop || do_service_status) {
        if (!is_elevated()) {
            std::cerr << "Service commands require Administrator privileges.\n"
                         "Please run the shell as Administrator and try again."
                      << std::endl;
            return 1;
        }
        std::wstring exe = get_module_path();
        if (!file_exists_w(exe)) {
            arc::log::error("Service: executable path does not exist");
            return 1;
        }
        std::wstring cmd = L"\"" + exe + L"\" --service";
        if (!config_path.empty()) {
            if (is_safe_arg_utf8(config_path)) {
                cmd += L" --config \"" + to_w(config_path) + L"\"";
            } else {
                arc::log::warn("Service: unsafe characters in config path; skipping --config");
            }
        }
        bool ok = true;
        if (do_install)
            ok = ok && arc::service::install(svcName, L"Alt Right Click", cmd);
        if (do_uninstall)
            ok = ok && arc::service::uninstall(svcName);
        if (do_start)
            ok = ok && arc::service::start(svcName);
        if (do_stop)
            ok = ok && arc::service::stop(svcName);
        if (do_service_status) {
            bool running = arc::service::is_running(svcName);
            std::cout << (running ? "RUNNING" : "STOPPED") << std::endl;
            ok = ok && running;
        }
        std::cout << (ok ? "OK" : "FAILED") << std::endl;
        return ok ? 0 : 1;
    }

    // Scheduled Task management
    const std::wstring taskName = L"AltRightClickTask";
    if (do_task_install || do_task_uninstall || do_task_update || do_task_status) {
        std::wstring exe = get_module_path();
        std::wstring cmd = L"\"" + exe + L"\"";
        if (!config_path.empty()) {
            cmd += L" --config \"" + to_w(config_path) + L"\"";
        }
        bool ok = true;
        if (do_task_install)
            ok = ok && arc::task::install(taskName, cmd, true);
        if (do_task_uninstall)
            ok = ok && arc::task::uninstall(taskName);
        if (do_task_update)
            ok = ok && arc::task::update(taskName, cmd, true);
        if (do_task_status) {
            bool exists = arc::task::exists(taskName);
            std::cout << (exists ? "PRESENT" : "MISSING") << std::endl;
            ok = ok && exists;
        }
        std::cout << (ok ? "OK" : "FAILED") << std::endl;
        return ok ? 0 : 1;
    }

    if (run_as_service) {
        return arc::service::run(svcName);
    }

    // Normal interactive app: enforce single instance, load config, init hook, tray, message loop
    arc::singleton::SingletonGuard instance(arc::singleton::default_name());
    if (!instance.acquired()) {
        arc::log::warn("altrightclick is already running.");
        return 0;
    }
    // Auto-create default config on first run if missing
    {
        std::ifstream f(config_path);
        if (!f.good()) {
            arc::config::Config defaults;
            arc::config::save(config_path, defaults);
        }
    }
    // Install console control handler early so Ctrl+C/close triggers clean exit.
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    arc::config::Config cfg = arc::config::load(config_path);
    if (!cli_log_level.empty())
        cfg.log_level = cli_log_level;
    if (!cli_log_file.empty())
        cfg.log_file = cli_log_file;
    arc::log::set_level_by_name(cfg.log_level);
    if (!cfg.log_file.empty())
        arc::log::set_file(cfg.log_file);
    arc::log::info(std::string("altrightclick ") + ARC_VERSION);
    arc::log::info(std::string("Using config: ") + config_path);
    arc::hook::apply_hook_config(cfg);
    arc::log::start_async();

    if (!cfg.enabled) {
        arc::log::info("altrightclick: disabled in config.");
        return 0;
    }

    // Start hook + tray workers
    if (!arc::hook::start()) {
        arc::log::error("Failed to start hook worker");
        return 1;
    }
    std::atomic<bool> exitRequested{false};
    std::filesystem::path config_path_fs = std::filesystem::path(config_path);
    arc::tray::TrayContext trayCtx{cfg, config_path_fs, exitRequested};
    if (cfg.show_tray) {
        arc::tray::start(L"AltRightClick running (Alt+Left => Right)", &trayCtx);
    }

    // Optional live reload watcher
    std::atomic<bool> watchStop{false};
    std::thread watchThread;
    auto start_watch = [&]() {
        if (!cfg.watch_config)
            return;
        watchThread = std::thread([&]() {
            auto get_mtime = [](const std::wstring &p) -> ULONGLONG {
                WIN32_FILE_ATTRIBUTE_DATA fad{};
                if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) {
                    ULARGE_INTEGER ui;
                    ui.HighPart = fad.ftLastWriteTime.dwHighDateTime;
                    ui.LowPart = fad.ftLastWriteTime.dwLowDateTime;
                    return ui.QuadPart;
                }
                return 0ULL;
            };
            std::wstring wcfg = to_w(config_path);
            ULONGLONG last = get_mtime(wcfg);
            while (!watchStop.load()) {
                Sleep(500);
                ULONGLONG cur = get_mtime(wcfg);
                if (cur != 0 && cur != last) {
                    last = cur;
                    arc::config::Config newCfg = arc::config::load(config_path);
                    if (!cli_log_level.empty())
                        newCfg.log_level = cli_log_level;
                    if (!cli_log_file.empty())
                        newCfg.log_file = cli_log_file;
                    arc::log::set_level_by_name(newCfg.log_level);
                    if (!newCfg.log_file.empty())
                        arc::log::set_file(newCfg.log_file);
                    arc::hook::apply_hook_config(newCfg);
                    trayCtx.cfg = newCfg;
                    arc::tray::notify(L"altrightclick", L"Configuration reloaded");
                    arc::log::info("Configuration reloaded");
                }
            }
        });
    };
    start_watch();

    // Controller: poll for exit key or tray Exit
    arc::log::info("Alt + Left Click => Right Click. Press exit key to quit.");
    while (true) {
        if (exitRequested.load())
            break;
        if (g_console_shutdown.load())
            break;
        if (cfg.exit_vk && (GetAsyncKeyState(static_cast<int>(cfg.exit_vk)) & 0x8000))
            break;
        Sleep(50);
    }

    watchStop.store(true);
    if (watchThread.joinable())
        watchThread.join();
    arc::tray::stop();
    arc::hook::stop();
    arc::log::stop_async();
    return 0;
}

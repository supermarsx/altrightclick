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
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cstdio>

#include "arc/hook.h"
#include "arc/tray.h"
#include "arc/config.h"
#include "arc/persistence.h"
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

/** Returns a readable name for the configured trigger. */
static const char *trigger_name(arc::config::Config::Trigger t) {
    switch (t) {
    case arc::config::Config::Trigger::Left:
        return "LEFT";
    case arc::config::Config::Trigger::Middle:
        return "MIDDLE";
    case arc::config::Config::Trigger::X1:
        return "X1";
    case arc::config::Config::Trigger::X2:
        return "X2";
    }
    return "UNKNOWN";
}

/** Escapes arbitrary UTF-8 for safe JSON string output. */
static std::string escape_json(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04X", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

/** Formats a system_clock time in ISO-8601 UTC (e.g., 2024-05-15T12:00:00Z). */
static std::string to_iso8601(const std::chrono::system_clock::time_point &tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_s(&tm, &tt);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
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
                 "  --persistence-enable   Enable persistence monitor for this run (overrides config)\n"
                 "  --persistence-disable  Disable persistence monitor for this run (overrides config)\n"
                 "  --launched-by-monitor  Internal; suppress spawning a nested monitor when revived\n"
                 "  --task-install         Install Scheduled Task (on logon, highest privs)\n"
                 "  --task-uninstall       Uninstall Scheduled Task\n"
                 "  --task-update          Update Scheduled Task target/args\n"
                 "  --task-status          Check if Scheduled Task exists\n"
                 "  --status               Print human-readable runtime/config status\n"
                 "  --status-json          Print status as JSON (mutually implies --status)\n"
                 "  --help                 Show this help\n";
}

/** Program entry point. Parses CLI, starts workers, and coordinates shutdown. */
int main(int argc, char **argv) {
    std::string config_path = arc::config::default_path().u8string();

    // Parse args
    bool do_install = false, do_uninstall = false, do_start = false, do_stop = false, do_service_status = false,
         run_as_service = false;
    bool run_as_monitor = false;
    bool launched_by_monitor = false;
    unsigned long monitor_parent_pid = 0;
    bool do_task_install = false, do_task_uninstall = false, do_task_update = false, do_task_status = false;
    bool do_status = false;
    bool do_status_json = false;
    std::string cli_log_level;
    std::string cli_log_file;
    bool do_generate_config = false;
    int cli_persistence = -1;  // -1: no override, 0: disable, 1: enable
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
        } else if (a == "--persistence-enable") {
            cli_persistence = 1;
        } else if (a == "--persistence-disable" || a == "--no-persistence") {
            cli_persistence = 0;
        } else if (a == "--launched-by-monitor") {
            launched_by_monitor = true;
        } else if (a == "--task-install") {
            do_task_install = true;
        } else if (a == "--task-uninstall") {
            do_task_uninstall = true;
        } else if (a == "--task-update") {
            do_task_update = true;
        } else if (a == "--task-status") {
            do_task_status = true;
        } else if (a == "--monitor") {
            run_as_monitor = true;
        } else if (a == "--parent" && i + 1 < argc) {
            try { monitor_parent_pid = static_cast<unsigned long>(std::stoul(argv[++i])); } catch (...) { monitor_parent_pid = 0; }
        } else if (a == "--status") {
            do_status = true;
        } else if (a == "--status-json") {
            do_status = true;
            do_status_json = true;
        } else if (a == "--help" || a == "-h" || a == "-?") {
            print_help();
            return 0;
        }
    }

    // Monitor mode: run persistence monitor and exit
    if (run_as_monitor) {
        std::wstring exe = get_module_path();
        return arc::persistence::run_monitor(monitor_parent_pid, exe, to_w(config_path));
    }

    const std::wstring svcName = L"AltRightClickService";
    const std::wstring taskName = L"AltRightClickTask";

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

    if (do_status) {
        bool cfg_exists = std::filesystem::exists(config_path);
        arc::config::Config status_cfg;
        if (cfg_exists) {
            status_cfg = arc::config::load(config_path);
        }
        bool interactive_running = false;
        {
            arc::singleton::SingletonGuard probe(arc::singleton::default_name());
            interactive_running = !probe.acquired();
        }
        bool monitor_running = arc::persistence::is_monitor_running();
        bool service_running = arc::service::is_running(svcName);
        bool task_present = arc::task::exists(taskName);
        auto history = arc::persistence::restart_history();
        std::string history_last = history.empty() ? "" : to_iso8601(history.back());
        auto bool_word = [](bool v) { return v ? "true" : "false"; };
        if (do_status_json) {
            std::ostringstream oss;
            oss << "{";
            oss << "\"config_path\":\"" << escape_json(config_path) << "\",";
            oss << "\"config_exists\":" << (cfg_exists ? "true" : "false") << ",";
            oss << "\"enabled\":" << (status_cfg.enabled ? "true" : "false") << ",";
            oss << "\"show_tray\":" << (status_cfg.show_tray ? "true" : "false") << ",";
            oss << "\"modifier_vk\":" << status_cfg.modifier_vk << ",";
            oss << "\"modifier_combo_vks\":[";
            for (size_t i = 0; i < status_cfg.modifier_combo_vks.size(); ++i) {
                if (i)
                    oss << ",";
                oss << status_cfg.modifier_combo_vks[i];
            }
            oss << "],";
            oss << "\"trigger\":\"" << trigger_name(status_cfg.trigger) << "\",";
            oss << "\"watch_config\":" << (status_cfg.watch_config ? "true" : "false") << ",";
            oss << "\"log_thread_id\":" << (status_cfg.log_thread_id ? "true" : "false") << ",";
            oss << "\"persistence_enabled\":" << (status_cfg.persistence_enabled ? "true" : "false") << ",";
            oss << "\"interactive_running\":" << (interactive_running ? "true" : "false") << ",";
            oss << "\"service_running\":" << (service_running ? "true" : "false") << ",";
            oss << "\"scheduled_task_present\":" << (task_present ? "true" : "false") << ",";
            oss << "\"monitor_running\":" << (monitor_running ? "true" : "false") << ",";
            oss << "\"restart_history_count\":" << history.size() << ",";
            oss << "\"restart_history_last\":";
            if (history.empty())
                oss << "null";
            else
                oss << "\"" << escape_json(history_last) << "\"";
            oss << "}";
            std::cout << oss.str() << std::endl;
        } else {
            std::cout << "config_path=" << config_path << (cfg_exists ? "" : " (missing)") << "\n";
            std::cout << "enabled=" << bool_word(status_cfg.enabled) << "\n";
            std::cout << "show_tray=" << bool_word(status_cfg.show_tray) << "\n";
            std::cout << "modifier_vk=0x" << std::hex << status_cfg.modifier_vk << std::dec << "\n";
            std::cout << "modifier_combo_count=" << status_cfg.modifier_combo_vks.size() << "\n";
            std::cout << "trigger=" << trigger_name(status_cfg.trigger) << "\n";
            std::cout << "watch_config=" << bool_word(status_cfg.watch_config) << "\n";
            std::cout << "log_thread_id=" << bool_word(status_cfg.log_thread_id) << "\n";
            std::cout << "persistence_enabled=" << bool_word(status_cfg.persistence_enabled) << "\n";
            std::cout << "interactive_running=" << bool_word(interactive_running) << "\n";
            std::cout << "service_running=" << bool_word(service_running) << "\n";
            std::cout << "scheduled_task_present=" << bool_word(task_present) << "\n";
            std::cout << "monitor_running=" << bool_word(monitor_running) << "\n";
            std::cout << "restart_history_count=" << history.size() << "\n";
            std::cout << "restart_history_last=" << (history.empty() ? "none" : history_last) << "\n";
        }
        return 0;
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
    if (cli_persistence != -1)
        cfg.persistence_enabled = (cli_persistence == 1);
    arc::log::set_level_by_name(cfg.log_level);
    arc::log::set_include_thread_id(cfg.log_thread_id);
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
    // Optionally start persistence monitor (detached process) to revive the app if it crashes
    if (cfg.persistence_enabled && !launched_by_monitor) {
        std::wstring exe = get_module_path();
        arc::persistence::spawn_monitor(exe, config_path);
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
                    arc::log::set_include_thread_id(newCfg.log_thread_id);
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
    // Mark intentional exit so persistence monitor (if any) does not relaunch us
    arc::persistence::write_intent_marker();
    return 0;
}

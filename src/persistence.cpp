/**
 * @file persistence.cpp
 * @brief Simple monitor process to keep the app alive on crashes.
 */

#include "arc/persistence.h"

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <algorithm>

#include "arc/log.h"
#include "arc/config.h"

namespace arc::persistence {

/// Tracks the PID of the currently running monitor child (if any).
static std::atomic<DWORD> g_monitorPid{0};

/**
 * @brief Returns the named event used to signal a monitor stop.
 *
 * @param parentPid PID of the monitored parent process.
 * @return Event name suitable for CreateEventW/GetEventByName.
 */
static std::wstring stop_event_name(DWORD parentPid) {
    return L"Local\\altrightclick_stop_" + std::to_wstring(parentPid);
}

/**
 * @brief Wrap a path in quotes if it contains whitespace.
 */
static std::wstring quote_if_needed(const std::wstring &s) {
    if (s.find(L' ') != std::wstring::npos)
        return L"\"" + s + L"\"";
    return s;
}

/**
 * @brief Launch a detached monitor instance tracking the current process.
 */
bool spawn_monitor(const std::wstring &exe_path, const std::string &config_path) {
    DWORD pid = GetCurrentProcessId();
    std::wstring cmd = quote_if_needed(exe_path) + L" --monitor --parent " + std::to_wstring(pid);
    if (!config_path.empty()) {
        std::wstring wcfg(config_path.begin(), config_path.end());
        cmd += L" --config \"" + wcfg + L"\"";
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    BOOL ok = CreateProcessW(/*lpApplicationName*/ nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) {
        arc::log::warn("persistence: failed to spawn monitor: " + arc::log::last_error_message(GetLastError()));
        return false;
    }
    g_monitorPid.store(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    arc::log::info("persistence: monitor started");
    return true;
}

/**
 * @brief Check if the previously spawned monitor process is still alive.
 */
bool is_monitor_running() {
    DWORD pid = g_monitorPid.load();
    if (!pid)
        return false;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h)
        return false;
    DWORD code = 0;
    bool running = false;
    if (GetExitCodeProcess(h, &code)) {
        running = (code == STILL_ACTIVE);
    }
    CloseHandle(h);
    if (!running) g_monitorPid.store(0);
    return running;
}

bool stop_monitor_graceful(unsigned int timeout_ms) {
    DWORD pid = g_monitorPid.load();
    // Signal stop event named by current process id (parent id used by monitor)
    HANDLE hEvt = CreateEventW(nullptr, TRUE, FALSE, stop_event_name(GetCurrentProcessId()).c_str());
    if (hEvt) {
        SetEvent(hEvt);
        CloseHandle(hEvt);
    }
    if (!pid)
        return true;
    HANDLE hProc = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
    if (!hProc)
        return false;
    DWORD wr = WaitForSingleObject(hProc, timeout_ms);
    if (wr == WAIT_OBJECT_0) {
        CloseHandle(hProc);
        g_monitorPid.store(0);
        return true;
    }
    // Fallback: force terminate
    BOOL ok = TerminateProcess(hProc, 0);
    CloseHandle(hProc);
    if (ok) g_monitorPid.store(0);
    return ok != 0;
}

/**
 * @brief Wait for a process to exit and return its code.
 *
 * @param pid Process identifier to wait on.
 * @return Exit code (STILL_ACTIVE if the process cannot be opened).
 */
static DWORD wait_process(DWORD pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return STILL_ACTIVE;  // treat as active; we'll just proceed
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    if (!GetExitCodeProcess(h, &code)) code = 0;
    CloseHandle(h);
    return code;
}

/**
 * @brief Spawn a child process monitored by the persistence helper.
 *
 * @param exe_path Path to the executable to relaunch.
 * @param config_path Optional config path to pass via CLI.
 * @param out_pi Receives PROCESS_INFORMATION for the spawned child.
 * @return 0 on success, -1 on failure.
 */
static DWORD spawn_child(const std::wstring &exe_path, const std::wstring &config_path, PROCESS_INFORMATION *out_pi) {
    std::wstring cmd = quote_if_needed(exe_path) + L" --launched-by-monitor";
    if (!config_path.empty()) {
        cmd += L" --config \"" + config_path + L"\"";
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &si, &pi);
    if (!ok) {
        arc::log::warn("persistence: failed to relaunch app: " + arc::log::last_error_message(GetLastError()));
        return (DWORD)-1;
    }
    *out_pi = pi;
    return 0;
}

/**
 * @brief Entry point for the monitor child process.
 */

/**
 * @brief Compute the restart-history log path.
 */
static std::filesystem::path restart_history_path() {
    return std::filesystem::path(appdata_dir()) / "restart_history.log";
}

/**
 * @brief Load restart timestamps from persistent storage.
 */
static std::vector<std::chrono::system_clock::time_point> load_restart_history(const std::filesystem::path &path) {
    std::vector<std::chrono::system_clock::time_point> entries;
    std::ifstream in(path);
    if (!in.is_open())
        return entries;
    std::string line;
    while (std::getline(in, line)) {
        try {
            long long secs = std::stoll(line);
            entries.emplace_back(std::chrono::system_clock::time_point(std::chrono::seconds(secs)));
        } catch (...) {
        }
    }
    return entries;
}

/**
 * @brief Persist restart timestamps for future monitor invocations.
 */
static void save_restart_history(const std::filesystem::path &path,
                                 const std::vector<std::chrono::system_clock::time_point> &entries) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
        return;
    for (auto &tp : entries) {
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
        out << secs << "\n";
    }
}

/**
 * @brief Delete the restart history log (used on clean shutdown).
 */
static void clear_restart_history(const std::filesystem::path &path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

int run_monitor(unsigned long parent_pid, const std::wstring &exe_path, const std::wstring &config_path) {
    // Load persistence tuning from config if provided
    int maxRestarts = 5;
    int windowSec = 60;
    int backoffMs = 1000;
    int backoffMaxMs = 30000;
    if (!config_path.empty()) {
        std::filesystem::path p(config_path);
        arc::config::Config cfg = arc::config::load(p);
        maxRestarts = cfg.persistence_max_restarts;
        windowSec = cfg.persistence_window_sec;
        backoffMs = cfg.persistence_backoff_ms;
        backoffMaxMs = cfg.persistence_backoff_max_ms;
    }
    // Wait for parent exit or a graceful-stop signal
    HANDLE hParent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(parent_pid));
    std::wstring stopName = stop_event_name(static_cast<DWORD>(parent_pid));
    HANDLE hStop = CreateEventW(nullptr, TRUE, FALSE, stopName.c_str());
    if (hParent) {
        HANDLE hsWait[2] = {hParent, hStop};
        WaitForMultipleObjects(2, hsWait, FALSE, INFINITE);
        CloseHandle(hParent);
        if (WaitForSingleObject(hStop, 0) == WAIT_OBJECT_0) {
            CloseHandle(hStop);
            return 0;
        }
    }

    // Restart loop with simple backoff and restart cap
    const int kMaxRestarts = maxRestarts;
    const auto kWindow = std::chrono::seconds(windowSec);
    std::filesystem::path history_path = restart_history_path();
    std::vector<std::chrono::system_clock::time_point> restarts = load_restart_history(history_path);
    std::chrono::milliseconds backoff(backoffMs);
    const std::chrono::milliseconds backoff_max(backoffMaxMs);

    while (true) {
        // Clear stale intent marker before we (re)launch a child
        {
            std::wstring p = intent_marker_path();
            DeleteFileW(p.c_str());
        }
        // Enforce max restarts in window
        auto now = std::chrono::system_clock::now();
        size_t before = restarts.size();
        restarts.erase(std::remove_if(restarts.begin(), restarts.end(), [&](auto t) { return now - t > kWindow; }),
                       restarts.end());
        if (restarts.size() != before) {
            save_restart_history(history_path, restarts);
        }
        if (static_cast<int>(restarts.size()) >= kMaxRestarts) {
            arc::log::warn("persistence: too many restarts; sleeping for a minute");
            std::this_thread::sleep_for(kWindow);
            continue;
        }

        PROCESS_INFORMATION pi{};
        if (spawn_child(exe_path, config_path, &pi) != 0) {
            std::this_thread::sleep_for(backoff);
            backoff = std::min(backoff * 2, backoff_max);
            continue;
        }
        CloseHandle(pi.hThread);
        DWORD exit_code = 0;
        // Wait for child exit or stop signal
        HANDLE hsChild[2] = {pi.hProcess, hStop};
        DWORD wr = WaitForMultipleObjects(2, hsChild, FALSE, INFINITE);
        if (wr == WAIT_OBJECT_0 + 1) {
            CloseHandle(pi.hProcess);
            clear_restart_history(history_path);
            CloseHandle(hStop);
            return 0;
        }
        if (!GetExitCodeProcess(pi.hProcess, &exit_code)) exit_code = 1;
        CloseHandle(pi.hProcess);

        // If the app wrote the intent marker, treat as intentional exit regardless of code
        bool intentional = false;
        {
            std::wstring p = intent_marker_path();
            DWORD attrs = GetFileAttributesW(p.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                intentional = true;
                DeleteFileW(p.c_str());
            }
        }
        if (exit_code == 0 || intentional) {
            // Normal shutdown; stop monitoring
            arc::log::info("persistence: child exited normally; stopping monitor");
            break;
        }
        arc::log::warn("persistence: child exited abnormally; restarting...");
        restarts.push_back(std::chrono::system_clock::now());
        save_restart_history(history_path, restarts);
        std::this_thread::sleep_for(backoff);
        backoff = std::min(backoff * 2, backoff_max);
    }

    clear_restart_history(history_path);
    CloseHandle(hStop);
    return 0;
}

}  // namespace arc::persistence
namespace arc::persistence {

/**
 * @brief Resolve and create (if needed) the application data directory.
 *
 * @return UTF-16 path to %APPDATA%\\altrightclick (or "." on failure).
 */
static std::wstring appdata_dir() {
    PWSTR appdataW = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdataW))) {
        std::wstring wpath = std::wstring(appdataW) + L"\\altrightclick";
        CoTaskMemFree(appdataW);
        CreateDirectoryW(wpath.c_str(), nullptr);
        return wpath;
    }
    return L".";
}

/**
 * @brief Compute the marker path indicating intentional exit.
 */
std::wstring intent_marker_path() {
    return appdata_dir() + L"\\intentional_exit";
}

/**
 * @brief Create (or overwrite) the intentional-exit marker file.
 */
void write_intent_marker() {
    std::wstring p = intent_marker_path();
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
}

std::vector<std::chrono::system_clock::time_point> restart_history() {
    return load_restart_history(restart_history_path());
}
}  // namespace arc::persistence

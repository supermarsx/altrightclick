/**
 * @file persistence.cpp
 * @brief Simple monitor process to keep the app alive on crashes.
 */

#include "arc/persistence.h"

#include <windows.h>

#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include "arc/log.h"

namespace arc::persistence {

static std::wstring quote_if_needed(const std::wstring &s) {
    if (s.find(L' ') != std::wstring::npos)
        return L"\"" + s + L"\"";
    return s;
}

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
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    arc::log::info("persistence: monitor started");
    return true;
}

static DWORD wait_process(DWORD pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return STILL_ACTIVE;  // treat as active; we'll just proceed
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    if (!GetExitCodeProcess(h, &code)) code = 0;
    CloseHandle(h);
    return code;
}

static DWORD spawn_child(const std::wstring &exe_path, const std::wstring &config_path, PROCESS_INFORMATION *out_pi) {
    std::wstring cmd = quote_if_needed(exe_path);
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

int run_monitor(unsigned long parent_pid, const std::wstring &exe_path, const std::wstring &config_path) {
    // Wait for the parent process to exit
    (void)wait_process(static_cast<DWORD>(parent_pid));

    // Restart loop with simple backoff and restart cap
    const int kMaxRestarts = 5;
    const auto kWindow = std::chrono::seconds(60);
    std::vector<std::chrono::steady_clock::time_point> restarts;
    std::chrono::milliseconds backoff(1000);
    const std::chrono::milliseconds backoff_max(30000);

    while (true) {
        // Enforce max restarts in window
        auto now = std::chrono::steady_clock::now();
        restarts.erase(std::remove_if(restarts.begin(), restarts.end(), [&](auto t) { return now - t > kWindow; }),
                       restarts.end());
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
        WaitForSingleObject(pi.hProcess, INFINITE);
        if (!GetExitCodeProcess(pi.hProcess, &exit_code)) exit_code = 1;
        CloseHandle(pi.hProcess);

        if (exit_code == 0) {
            // Normal shutdown; stop monitoring
            arc::log::info("persistence: child exited normally; stopping monitor");
            break;
        }
        arc::log::warn("persistence: child exited abnormally; restarting...");
        restarts.push_back(std::chrono::steady_clock::now());
        std::this_thread::sleep_for(backoff);
        backoff = std::min(backoff * 2, backoff_max);
    }

    return 0;
}

}  // namespace arc::persistence


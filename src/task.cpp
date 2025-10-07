/**
 * @file task.cpp
 * @brief Windows Scheduled Task helpers (schtasks) implementation.
 *
 * Uses CreateProcessW to invoke schtasks.exe with appropriate arguments for
 * creating, deleting, querying, and updating tasks. Success is based on the
 * process exit code. All strings are wide since schtasks.exe expects UTF-16.
 */

#include "arc/task.h"

#include <windows.h>

#include <string>

#include "arc/log.h"

namespace {

/**
 * @brief Adds quotes around a string if it contains spaces.
 * @param s Input string.
 * @return Quoted string when needed; otherwise original string.
 */
std::wstring quote(const std::wstring &s) {
    if (s.find(L' ') != std::wstring::npos)
        return L"\"" + s + L"\"";
    return s;
}

/**
 * @brief Runs schtasks.exe with the provided argument tail.
 *
 * @param args Argument string (e.g., "/Create ...").
 * @return true if schtasks.exe returns exit code 0.
 */
bool run_schtasks(const std::wstring &args) {
    std::wstring cmd = L"schtasks.exe " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &pi)) {
        arc::log::error("CreateProcessW(schtasks) failed: " + arc::log::last_error_message(GetLastError()));
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) {
        arc::log::error("schtasks exited with code " + std::to_string(code));
    }
    return code == 0;
}

}  // namespace

namespace arc::task {

/** Creates a logon-triggered scheduled task for this user. */
bool install(const std::wstring &name, const std::wstring &exe_with_args, bool highest) {
    std::wstring rl = highest ? L" /RL HIGHEST" : L"";
    std::wstring cmd = L"/Create /TN " + quote(name) + L" /TR " + quote(exe_with_args) + L" /SC ONLOGON /F /IT" + rl;
    return run_schtasks(cmd);
}

/** Deletes the scheduled task if present. */
bool uninstall(const std::wstring &name) { return run_schtasks(L"/Delete /TN " + quote(name) + L" /F"); }

/** Returns true if the named task exists. */
bool exists(const std::wstring &name) {
    // schtasks returns non-zero if not found
    return run_schtasks(L"/Query /TN " + quote(name));
}

/** Recreates the scheduled task with updated parameters. */
bool update(const std::wstring &name, const std::wstring &exe_with_args, bool highest) {
    // Recreate task with new action/settings
    (void)uninstall(name);
    return install(name, exe_with_args, highest);
}

}  // namespace arc::task


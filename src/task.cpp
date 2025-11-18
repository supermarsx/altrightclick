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
 *
 * Many command-line programs (including schtasks.exe) expect arguments that
 * contain spaces to be quoted. This helper returns a copy of @p s wrapped in
 * double quotes if the string contains any space characters; otherwise it
 * returns the original string unchanged.
 *
 * @param s Input wide string to inspect.
 * @return std::wstring Quoted string when needed; otherwise the original
 *         string is returned.
 */
std::wstring quote(const std::wstring &s) {
    if (s.find(L' ') != std::wstring::npos)
        return L"\"" + s + L"\"";
    return s;
}

/**
 * @brief Invoke schtasks.exe with the supplied argument tail.
 *
 * This function constructs a command line of the form "schtasks.exe <args>"
 * and executes it using CreateProcessW. The process is run hidden (no window)
 * and this call blocks until the schtasks process exits. The result is
 * determined by the child process exit code (0 = success).
 *
 * Standard error conditions (CreateProcessW failures and non-zero exit codes)
 * are logged via arc::log::error with additional context where available.
 *
 * @param args Tail of command-line arguments passed to schtasks.exe (UTF-16).
 *             Example: L"/Create /TN <name> /TR <exe> /SC ONLOGON /F /IT".
 * @return true if schtasks.exe returned exit code 0, false on failure or when
 *         schtasks returns a non-zero exit code.
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

/**
 * @brief Creates a logon-triggered scheduled task for the current user.
 *
 * This wraps schtasks.exe /Create with the appropriate options to create a
 * scheduled task that runs on user logon. The task name and action command
 * (executable and arguments) are provided by the caller. Optionally requests
 * highest run level (elevated) if @p highest is true.
 *
 * @param name Task name (task folder path acceptable) as a UTF-16 string.
 * @param exe_with_args Full command to run for the task (executable and any
 *                      arguments). Will be quoted if it contains spaces.
 * @param highest If true, the task is created with /RL HIGHEST to request
 *                highest available privileges.
 * @return true on success (schtasks returned 0), false otherwise.
 */
bool install(const std::wstring &name, const std::wstring &exe_with_args, bool highest) {
    std::wstring rl = highest ? L" /RL HIGHEST" : L"";
    std::wstring cmd = L"/Create /TN " + quote(name) + L" /TR " + quote(exe_with_args) + L" /SC ONLOGON /F /IT" + rl;
    return run_schtasks(cmd);
}

/**
 * @brief Deletes the scheduled task with the given name if it exists.
 *
 * This calls schtasks.exe /Delete /TN <name> /F which force-deletes the task
 * without prompting. The function returns true only if schtasks returns a
 * successful exit code.
 *
 * @param name Task name to delete.
 * @return true if the delete operation succeeded (or schtasks reported success),
 *         false otherwise.
 */
bool uninstall(const std::wstring &name) { return run_schtasks(L"/Delete /TN " + quote(name) + L" /F"); }

/**
 * @brief Returns true if the named scheduled task exists for the current user.
 *
 * This issues schtasks.exe /Query /TN <name> and treats a zero exit code as
 * evidence that the task exists. Non-zero indicates the task could not be
 * found or another error occurred.
 *
 * @param name Task name to query.
 * @return true if the task exists (schtasks returned 0), false otherwise.
 */
bool exists(const std::wstring &name) {
    // schtasks returns non-zero if not found
    return run_schtasks(L"/Query /TN " + quote(name));
}

/**
 * @brief Recreates the scheduled task with updated action/settings.
 *
 * This function uninstalls the existing task (ignoring errors) and then
 * installs a new task using the provided parameters. This is a convenience
 * helper to update a task's action or privileges since schtasks.exe does not
 * provide a single atomic "update" operation for all settings.
 *
 * @param name Task name to update.
 * @param exe_with_args New executable and arguments for the task action.
 * @param highest If true, create the task with the highest run level.
 * @return true if the resulting install succeeded, false otherwise.
 */
bool update(const std::wstring &name, const std::wstring &exe_with_args, bool highest) {
    // Recreate task with new action/settings
    (void)uninstall(name);
    return install(name, exe_with_args, highest);
}

}  // namespace arc::task

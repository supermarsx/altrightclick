#include "arc/task.h"

#include <windows.h>

#include <string>

#include "arc/log.h"

namespace {

std::wstring quote(const std::wstring &s) {
    if (s.find(L' ') != std::wstring::npos)
        return L"\"" + s + L"\"";
    return s;
}

bool run_schtasks(const std::wstring &args) {
    std::wstring cmd = L"schtasks.exe " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &pi)) {
        arc::log_error("CreateProcessW(schtasks) failed: " + arc::last_error_message(GetLastError()));
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) {
        arc::log_error("schtasks exited with code " + std::to_string(code));
    }
    return code == 0;
}

}  // namespace

namespace arc {

bool task_install(const std::wstring &name, const std::wstring &exe_with_args, bool highest) {
    std::wstring rl = highest ? L" /RL HIGHEST" : L"";
    std::wstring cmd = L"/Create /TN " + quote(name) + L" /TR " + quote(exe_with_args) + L" /SC ONLOGON /F /IT" + rl;
    return run_schtasks(cmd);
}

bool task_uninstall(const std::wstring &name) { return run_schtasks(L"/Delete /TN " + quote(name) + L" /F"); }

bool task_exists(const std::wstring &name) {
    // schtasks returns non-zero if not found
    return run_schtasks(L"/Query /TN " + quote(name));
}

bool task_update(const std::wstring &name, const std::wstring &exe_with_args, bool highest) {
    // Recreate task with new action/settings
    (void)task_uninstall(name);
    return task_install(name, exe_with_args, highest);
}

}  // namespace arc

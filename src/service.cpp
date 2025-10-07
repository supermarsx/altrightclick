/**
 * @file service.cpp
 * @brief Windows Service helper implementations and service main.
 */

#include "arc/service.h"

#include <windows.h>
#include <string>
#include <vector>

#include "arc/hook.h"
#include "arc/app.h"
#include "arc/log.h"
#include "arc/singleton.h"

namespace {

SERVICE_STATUS_HANDLE g_SvcStatusHandle = nullptr;
SERVICE_STATUS g_SvcStatus{};

/**
 * @brief Updates the SCM-visible service status fields.
 *
 * @param dwCurrentState A SERVICE_STATE value (e.g., SERVICE_RUNNING).
 * @param dwWin32ExitCode Win32 error code on failure, NO_ERROR otherwise.
 * @param dwWaitHint Estimated time for pending start/stop operations (ms).
 */
void ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {
    g_SvcStatus.dwCurrentState = dwCurrentState;
    g_SvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    g_SvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING) {
        g_SvcStatus.dwControlsAccepted = 0;
    } else {
        g_SvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    SetServiceStatus(g_SvcStatusHandle, &g_SvcStatus);
}

/**
 * @brief Receives control requests (stop/shutdown) from the SCM.
 * @param dwCtrl Control code (SERVICE_CONTROL_STOP, SERVICE_CONTROL_SHUTDOWN, ...).
 */
void WINAPI SvcCtrlHandler(DWORD dwCtrl) {
    switch (dwCtrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
        PostQuitMessage(0);
        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
        break;
    default:
        break;
    }
}

/**
 * @brief Service entrypoint registered with the SCM dispatcher.
 *
 * Installs singleton guard, starts the hook worker thread, reports status to
 * the SCM, and then pumps a message loop until WM_QUIT is posted (on stop).
 */
void WINAPI SvcMain(DWORD, LPTSTR *) {
    g_SvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_SvcStatus.dwServiceSpecificExitCode = 0;

    g_SvcStatusHandle = RegisterServiceCtrlHandlerW(L"AltRightClickService", SvcCtrlHandler);
    if (!g_SvcStatusHandle) {
        arc::log::error("RegisterServiceCtrlHandlerW failed: " + arc::log::last_error_message(GetLastError()));
        return;
    }

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Prevent multiple service instances (distinct from interactive singleton)
    {
        arc::singleton::SingletonGuard guard(arc::singleton::service_name());
        if (!guard.acquired()) {
            arc::log::warn("Service instance already running (singleton acquired by another process)");
            ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
            return;
        }
    }

    // Start hook worker on its own thread (installs its own hook + private message loop)
    if (!arc::hook::start()) {
        arc::log::error("Service: failed to start hook worker");
        ReportSvcStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 2);
        return;
    }

    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // Message loop for service control (WM_QUIT posted on stop)
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    arc::hook::stop();
    ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

/** Returns quoted string if it contains spaces. */
std::wstring quote(const std::wstring &s) {
    if (s.find(L' ') != std::wstring::npos) {
        return L"\"" + s + L"\"";
    }
    return s;
}

}  // namespace

namespace arc::service {

/**
 * Installs a Windows service.
 *
 * @param name Internal service name.
 * @param display_name Friendly name shown in Service Manager.
 * @param bin_path_with_args Fully quoted executable path followed by args.
 * @return true on success.
 */
bool install(const std::wstring &name, const std::wstring &display_name, const std::wstring &bin_path_with_args) {
    // Basic validation: ensure it starts with a quoted path and has no CR/LF
    if (bin_path_with_args.find(L"\n") != std::wstring::npos || bin_path_with_args.find(L"\r") != std::wstring::npos) {
        arc::log::error("service_install: binpath contains newline characters");
        return false;
    }
    if (bin_path_with_args.empty() || bin_path_with_args[0] != L'\"') {
        arc::log::error("service_install: binpath must start with a quoted executable path");
        return false;
    }
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        arc::log::error("OpenSCManagerW failed: " + arc::log::last_error_message(GetLastError()));
        return false;
    }

    SC_HANDLE svc = CreateServiceW(scm, name.c_str(), display_name.c_str(), SERVICE_ALL_ACCESS,
                                   SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                                   bin_path_with_args.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!svc) {
        arc::log::error("CreateServiceW failed: " + arc::log::last_error_message(GetLastError()));
        CloseServiceHandle(scm);
        return false;
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

/** Uninstalls a Windows service by internal name. */
bool uninstall(const std::wstring &name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        arc::log::error("OpenSCManagerW failed: " + arc::log::last_error_message(GetLastError()));
        return false;
    }
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), DELETE);
    if (!svc) {
        arc::log::error("OpenServiceW(DELETE) failed: " + arc::log::last_error_message(GetLastError()));
        CloseServiceHandle(scm);
        return false;
    }
    bool ok = DeleteService(svc) != 0;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

/**
 * @brief Starts a Windows service by internal name.
 * @param name Internal service name.
 * @return true on success.
 */
bool start(const std::wstring &name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        arc::log::error("OpenSCManagerW failed: " + arc::log::last_error_message(GetLastError()));
        return false;
    }
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_START);
    if (!svc) {
        arc::log::error("OpenServiceW(START) failed: " + arc::log::last_error_message(GetLastError()));
        CloseServiceHandle(scm);
        return false;
    }
    bool ok = StartServiceW(svc, 0, nullptr) != 0;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

/**
 * @brief Stops a Windows service by internal name.
 * @param name Internal service name.
 * @return true on success.
 */
bool stop(const std::wstring &name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        arc::log::error("OpenSCManagerW failed: " + arc::log::last_error_message(GetLastError()));
        return false;
    }
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_STOP);
    if (!svc) {
        arc::log::error("OpenServiceW(STOP) failed: " + arc::log::last_error_message(GetLastError()));
        CloseServiceHandle(scm);
        return false;
    }
    SERVICE_STATUS status{};
    bool ok = ControlService(svc, SERVICE_CONTROL_STOP, &status) != 0;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

/** Returns true if the service reports RUNNING. */
bool is_running(const std::wstring &name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return false;
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_QUERY_STATUS);
    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }
    SERVICE_STATUS_PROCESS ssp{};
    DWORD bytesNeeded = 0;
    bool running = false;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded)) {
        running = (ssp.dwCurrentState == SERVICE_RUNNING);
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return running;
}

/** Enters the service main loop by connecting to the SCM dispatcher. */
int run(const std::wstring &name) {
    SERVICE_TABLE_ENTRYW dispatchTable[] = {{const_cast<LPWSTR>(name.c_str()), (LPSERVICE_MAIN_FUNCTIONW)SvcMain},
                                            {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(dispatchTable)) {
        arc::log::error("StartServiceCtrlDispatcherW failed: " + arc::log::last_error_message(GetLastError()));
        return 1;
    }
    return 0;
}

}  // namespace arc::service

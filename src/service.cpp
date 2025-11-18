/**
 * @file service.cpp
 * @brief Windows Service helper implementations and service main.
 *
 * This translation unit implements helper routines used when running the
 * application as a Windows Service. It contains the service main routine
 * registered with the Service Control Manager (SCM), the control handler that
 * responds to stop/shutdown requests, and convenience wrappers for installing,
 * uninstalling and controlling the service via the SCM API.
 *
 * The service implementation uses a named singleton (mutex) to prevent
 * multiple service instances from running simultaneously. The runtime hook
 * worker (arc::hook) is started on service start and stopped on shutdown.
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
 * @brief Update the service status reported to the SCM.
 *
 * This helper sets the global SERVICE_STATUS structure fields and reports the
 * status to the Service Control Manager via SetServiceStatus. The helper also
 * adjusts the dwControlsAccepted mask based on whether the service is in a
 * pending start state.
 *
 * @param dwCurrentState Current service state (e.g. SERVICE_RUNNING).
 * @param dwWin32ExitCode The Win32 exit code to report on failure; NO_ERROR
 *                        when there is no error.
 * @param dwWaitHint Estimated time, in milliseconds, for a pending operation
 *                   to complete. Used when reporting start/stop pending states.
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
 * @brief Service control handler called by the SCM when control codes arrive.
 *
 * This function handles stop and shutdown control codes by posting a WM_QUIT
 * to the service's message loop and updating the service status appropriately.
 * Additional control codes are ignored.
 *
 * @param dwCtrl Control code received from SCM (SERVICE_CONTROL_STOP,
 *               SERVICE_CONTROL_SHUTDOWN, etc.).
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
 * @brief Entry point invoked by the SCM for this service.
 *
 * This is the function registered in the SERVICE_TABLE_ENTRY passed to
 * StartServiceCtrlDispatcher. Typical flow:
 *  - Register a control handler with RegisterServiceCtrlHandlerW.
 *  - Report start-pending status and install a per-machine singleton to prevent
 *    multiple service instances.
 *  - Start the runtime hook worker via arc::hook::start().
 *  - Report running status and enter a message loop until WM_QUIT is posted.
 *  - Stop the hook worker and report stopped status prior to exit.
 *
 * @note This function executes on a thread created by the SCM and must return
 *       only after the service has stopped.
 *
 * @param dwNumServicesArgc Number of arguments (unused).
 * @param lpServiceArgVectors Array of argument strings (unused).
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

/**
 * @brief Quote a wide string if it contains space characters.
 *
 * Utility used when constructing command lines for service binary path. If the
 * provided string contains whitespace it is returned enclosed in double quotes;
 * otherwise the original string is returned.
 *
 * @param s Input wide string.
 * @return std::wstring Possibly quoted string safe for use as a single command-line token.
 */
std::wstring quote(const std::wstring &s) {
    if (s.find(L' ') != std::wstring::npos) {
        return L"\"" + s + L"\"";
    }
    return s;
}

}  // namespace

namespace arc::service {

/**
 * @brief Install a Windows service with the specified parameters.
 *
 * This function opens a handle to the local Service Control Manager and calls
 * CreateServiceW to register a new service configured to start automatically.
 * It performs minimal validation on @p bin_path_with_args to guard against
 * accidental injection of newline characters and expects the executable path to
 * be quoted.
 *
 * @param name Internal (service) name used to identify the service.
 * @param display_name Friendly name displayed in the Services MMC snap-in.
 * @param bin_path_with_args Fully-quoted executable path optionally followed by arguments.
 * @return true if the service was successfully created; false otherwise.
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

/**
 * @brief Uninstall (delete) a Windows service by its internal name.
 *
 * Opens the SCM and the target service with DELETE access and attempts to remove
 * the service registration. The function returns true only if DeleteService
 * reports success.
 *
 * @param name Internal service name to delete.
 * @return true if the service was deleted successfully; false otherwise.
 */
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
 * @brief Start a Windows service by internal name.
 *
 * Attempts to open the service with SERVICE_START access and invoke StartServiceW.
 *
 * @param name Internal service name to start.
 * @return true if the operation succeeded; false otherwise.
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
 * @brief Stop a Windows service by internal name.
 *
 * Sends a SERVICE_CONTROL_STOP control to the named service and waits for the
 * control to be accepted by the service. The function returns true if the
 * control request was successfully issued.
 *
 * @param name Internal service name to stop.
 * @return true if the stop control was successfully sent; false otherwise.
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

/**
 * @brief Query whether the named service is currently running.
 *
 * Uses QueryServiceStatusEx with SC_STATUS_PROCESS_INFO to inspect the service's
 * current state and returns true when the state is SERVICE_RUNNING.
 *
 * @param name Internal service name to query.
 * @return true if the service reports SERVICE_RUNNING; false otherwise.
 */
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

/**
 * @brief Enter service main loop by connecting to the SCM dispatcher.
 *
 * Constructs a SERVICE_TABLE_ENTRY for the given service name and calls
 * StartServiceCtrlDispatcherW to hand control to the SCM. This function blocks
 * until the service has finished processing (i.e. SvcMain returns).
 *
 * @param name Internal service name to register with the SCM for dispatching.
 * @return int 0 on success (StartServiceCtrlDispatcherW returned true), non-zero
 *             on failure.
 */
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

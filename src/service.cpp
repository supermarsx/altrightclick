#include "arc/service.h"

#include <windows.h>
#include <string>
#include <vector>

#include "arc/hook.h"
#include "arc/app.h"

namespace {

SERVICE_STATUS_HANDLE g_SvcStatusHandle = nullptr;
SERVICE_STATUS g_SvcStatus{};

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

void WINAPI SvcMain(DWORD, LPTSTR *) {
    g_SvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_SvcStatus.dwServiceSpecificExitCode = 0;

    g_SvcStatusHandle = RegisterServiceCtrlHandlerW(L"AltRightClickService", SvcCtrlHandler);
    if (!g_SvcStatusHandle)
        return;

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    if (!arc::install_mouse_hook()) {
        ReportSvcStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 1);
        return;
    }

    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // Message loop
    arc::run_message_loop();

    arc::remove_mouse_hook();
    ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

std::wstring quote(const std::wstring &s) {
    if (s.find(L' ') != std::wstring::npos) {
        return L"\"" + s + L"\"";
    }
    return s;
}

}  // namespace

namespace arc {

bool service_install(const std::wstring &name, const std::wstring &display_name,
                     const std::wstring &bin_path_with_args) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm)
        return false;

    SC_HANDLE svc = CreateServiceW(scm, name.c_str(), display_name.c_str(), SERVICE_ALL_ACCESS,
                                   SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                                   bin_path_with_args.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

bool service_uninstall(const std::wstring &name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return false;
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), DELETE);
    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }
    bool ok = DeleteService(svc) != 0;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

bool service_start(const std::wstring &name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return false;
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_START);
    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }
    bool ok = StartServiceW(svc, 0, nullptr) != 0;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

bool service_stop(const std::wstring &name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return false;
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), SERVICE_STOP);
    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }
    SERVICE_STATUS status{};
    bool ok = ControlService(svc, SERVICE_CONTROL_STOP, &status) != 0;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

bool service_is_running(const std::wstring &name) {
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

int service_run(const std::wstring &name) {
    SERVICE_TABLE_ENTRYW dispatchTable[] = {{const_cast<LPWSTR>(name.c_str()), (LPSERVICE_MAIN_FUNCTIONW)SvcMain},
                                            {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(dispatchTable)) {
        return 1;
    }
    return 0;
}

}  // namespace arc

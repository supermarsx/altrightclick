/**
 * @file singleton.cpp
 * @brief Named mutex based process singleton implementation.
 *
 * This translation unit implements a small helper used to enforce a single
 * instance of a process or service using named Win32 mutex objects. It
 * provides a RAII guard which creates or opens a named mutex and records
 * whether the caller successfully acquired ownership (i.e. no previous
 * instance holds the mutex).
 *
 * The mutex name should include an explicit namespace prefix such as
 * "Local\\" (session-local) or "Global\\" (machine-wide) to control the
 * scope of the singleton. On success the mutex handle remains open for the
 * lifetime of the guard; closing the handle releases the mutex object.
 */

#include "arc/singleton.h"

namespace arc::singleton {

/**
 * @brief Creates/opens a named mutex and records ownership status.
 *
 * The constructor attempts to create a Win32 named mutex using CreateMutexW.
 * If the mutex did not previously exist the calling process is considered to
 * have acquired singleton ownership and the member @c acquired_ is set to
 * true. If the mutex already existed the call still returns a handle to the
 * existing object but @c acquired_ will be false. The handle remains open and
 * will be closed in the destructor.
 *
 * @param name Mutex name, including a scope prefix such as "Local\\" or
 *             "Global\\". Examples: "Local\\MyApp.Singleton" or
 *             "Global\\MyService.Singleton".
 *
 * @note It is the caller's responsibility to choose an appropriate namespace
 *       prefix. Session-local names avoid collisions between different user
 *       sessions on multi-user systems; global names are required when a
 *       machine-wide singleton (e.g. a service) is desired.
 *
 * @note When CreateMutexW returns successfully, call GetLastError() to detect
 *       whether the object already existed. If GetLastError() ==
 *       ERROR_ALREADY_EXISTS then the mutex was not created by this call and
 *       @c acquired_ will be false.
 */
SingletonGuard::SingletonGuard(const std::wstring &name) {
    // Create the named mutex. If it already exists, GetLastError() == ERROR_ALREADY_EXISTS.
    handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (handle_ != nullptr) {
        acquired_ = (GetLastError() != ERROR_ALREADY_EXISTS);
    }
}

/**
 * @brief Releases the mutex handle on destruction.
 *
 * The destructor closes the underlying mutex handle if it is valid. Closing
 * the handle will release the mutex object; if this instance held ownership
 * this allows another process to acquire the singleton.
 */
SingletonGuard::~SingletonGuard() {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

/**
 * @brief Returns the default per-session mutex name for the interactive app.
 *
 * This helper returns a recommended name using the "Local\\" namespace so
 * the singleton is scoped to the interactive user session. Use this name for
 * tray or desktop applications where multiple logged-in users should be able
 * to run the app independently.
 *
 * @return std::wstring Recommended mutex name for interactive per-session use.
 */
std::wstring default_name() {
    // Per-session uniqueness is typically preferred for tray apps.
    return L"Local\\AltRightClick.Singleton";
}

/**
 * @brief Returns the global mutex name for the service context.
 *
 * When the application runs as a service and must be unique across the
 * machine, use a name in the "Global\\" namespace. This helper returns such
 * a recommended name distinct from the interactive default.
 *
 * @return std::wstring Recommended mutex name for machine-global service use.
 */
std::wstring service_name() {
    // Global uniqueness for service context; distinct from interactive name
    return L"Global\\AltRightClick.Service";
}

}  // namespace arc::singleton

/**
 * @file singleton.cpp
 * @brief Named mutex based process singleton implementation.
 */

#include "arc/singleton.h"

namespace arc::singleton {

/**
 * Creates/opens a named mutex and records ownership status.
 *
 * @param name Mutex name, including scope prefix like "Local\\" or "Global\\".
 * @note If the mutex already exists, GetLastError() will return
 *       ERROR_ALREADY_EXISTS and @ref acquired_ remains false.
 */
SingletonGuard::SingletonGuard(const std::wstring &name) {
    // Create the named mutex. If it already exists, GetLastError() == ERROR_ALREADY_EXISTS.
    handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (handle_ != nullptr) {
        acquired_ = (GetLastError() != ERROR_ALREADY_EXISTS);
    }
}

/** Releases the mutex handle on destruction. */
SingletonGuard::~SingletonGuard() {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

/** Returns the default per-session mutex name for the interactive app. */
std::wstring default_name() {
    // Per-session uniqueness is typically preferred for tray apps.
    return L"Local\\AltRightClick.Singleton";
}

/** Returns the global mutex name for the service context. */
std::wstring service_name() {
    // Global uniqueness for service context; distinct from interactive name
    return L"Global\\AltRightClick.Service";
}

}  // namespace arc::singleton

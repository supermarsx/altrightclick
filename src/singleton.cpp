#include "arc/singleton.h"

namespace arc {

SingletonGuard::SingletonGuard(const std::wstring &name) {
    // Create the named mutex. If it already exists, GetLastError() == ERROR_ALREADY_EXISTS.
    handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
    if (handle_ != nullptr) {
        acquired_ = (GetLastError() != ERROR_ALREADY_EXISTS);
    }
}

SingletonGuard::~SingletonGuard() {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

std::wstring default_singleton_name() {
    // Per-session uniqueness is typically preferred for tray apps.
    return L"Local\\AltRightClick.Singleton";
}

}  // namespace arc

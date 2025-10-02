#pragma once

#include <windows.h>
#include <string>

namespace arc {

// RAII guard to enforce a single running instance using a named mutex.
class SingletonGuard {
 public:
    // Create a guard on a named mutex. Use a prefix like "Local\\" for per-session.
    explicit SingletonGuard(const std::wstring &name);
    ~SingletonGuard();

    // Returns true if this process acquired singleton ownership (i.e., no prior instance).
    bool acquired() const { return acquired_; }

 private:
    HANDLE handle_ = nullptr;
    bool acquired_ = false;
};

// Returns a sensible default name for the app (per-session uniqueness).
std::wstring default_singleton_name();

// Service scope singleton (global namespace, distinct from interactive app)
std::wstring service_singleton_name();

}  // namespace arc

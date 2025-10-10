/**
 * @file singleton.h
 * @brief Process singleton via named mutex (RAII guard).
 *
 * Provides a simple way to ensure only one interactive instance (per-session)
 * or service instance (global) is running by acquiring a named mutex.
 */
#pragma once

#include <windows.h>
#include <string>

namespace arc { namespace singleton {

/**
 * @class SingletonGuard
 * @brief RAII guard that enforces a single running instance via a named mutex.
 *
 * Use a name with the "Local\\" prefix for per-session uniqueness (typical for
 * interactive/tray apps) or "Global\\" for system-wide uniqueness (typical for
 * services). When the named mutex already exists, this instance will not acquire
 * ownership and @ref acquired returns false.
 */
class SingletonGuard {
 public:
    /**
     * @brief Creates/opens a named mutex and tries to acquire singleton ownership.
     *
     * @param name Mutex name, including a scope prefix like "Local\\" or "Global\\".
     */
    explicit SingletonGuard(const std::wstring &name);

    /**
     * @brief Releases the mutex handle on destruction.
     */
    ~SingletonGuard();

    /**
     * @brief Returns true if this process acquired singleton ownership.
     *
     * This indicates there was no prior instance already holding the named mutex.
     */
    bool acquired() const { return acquired_; }

 private:
    HANDLE handle_ = nullptr;   ///< OS handle to the named mutex (or nullptr on failure).
    bool acquired_ = false;     ///< True if this process owns the singleton.
};

/**
 * @brief Returns a sensible default name for the interactive app (per-session).
 * @return Name prefixed with "Local\\".
 */
std::wstring default_name();

/**
 * @brief Returns a global name for the service context.
 * @return Name prefixed with "Global\\".
 */
std::wstring service_name();

}  // namespace singleton

}  // namespace arc

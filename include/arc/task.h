/**
 * @file task.h
 * @brief Windows Scheduled Task helpers (schtasks.exe wrappers).
 *
 * Allows running the app on user logon without requiring service install. The
 * helpers shell out to schtasks.exe and return success based on exit codes.
 */
#pragma once

#include <string>

namespace arc { namespace task {

/**
 * @brief Creates a logon-triggered scheduled task for the current user.
 *
 * @param name          Task name as shown in Task Scheduler.
 * @param exe_with_args Fully quoted executable path and arguments for /TR.
 * @param highest       If true, requests highest privileges (/RL HIGHEST).
 * @return true on success.
 */
bool install(const std::wstring &name, const std::wstring &exe_with_args, bool highest = true);

/**
 * @brief Deletes the scheduled task if present.
 * @param name Task name.
 * @return true on success.
 */
bool uninstall(const std::wstring &name);

/**
 * @brief Returns true if the named scheduled task exists.
 * @param name Task name.
 */
bool exists(const std::wstring &name);

/**
 * @brief Recreates the task with updated action/settings.
 *
 * Deletes any existing task of the same name and installs a new one.
 *
 * @param name          Task name.
 * @param exe_with_args Fully quoted executable path and arguments for /TR.
 * @param highest       If true, requests highest privileges.
 * @return true on success.
 */
bool update(const std::wstring &name, const std::wstring &exe_with_args, bool highest = true);

}  // namespace task

}  // namespace arc
